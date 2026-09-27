from __future__ import annotations
import argparse, json, os, shutil, sqlite3, subprocess, sys, traceback, urllib.request, zipfile
from pathlib import Path

LLAMA_TAG="b10977"

def connect(path: str):
    db=sqlite3.connect(path, timeout=30)
    db.row_factory=sqlite3.Row
    return db

def update_job(db, job_id, *, state=None, progress=None, error=None):
    fields=[]; values=[]
    if state is not None:
        fields.append("state=?"); values.append(state)
        if state=="RUNNING": fields.append("started_utc=CURRENT_TIMESTAMP")
        if state in ("COMPLETED","FAILED","CANCELLED"): fields.append("completed_utc=CURRENT_TIMESTAMP")
    if progress is not None:
        fields.append("progress=?"); values.append(int(progress))
    if error is not None:
        fields.append("error_text=?"); values.append(str(error))
    values.append(job_id)
    db.execute(f"UPDATE trainer_jobs SET {','.join(fields)} WHERE id=?", values)
    db.commit()

def load_job(db, job_id):
    row=db.execute("SELECT * FROM trainer_jobs WHERE id=?", (job_id,)).fetchone()
    if not row: raise RuntimeError(f"Trainer job not found: {job_id}")
    return row

def load_foundation(db, foundation_id):
    if not foundation_id: return None
    return db.execute("SELECT * FROM model_foundations WHERE id=?", (foundation_id,)).fetchone()

def require_training_packages():
    try:
        import torch
        import transformers
        import datasets
        import peft
        return torch, transformers, datasets, peft
    except Exception as e:
        raise RuntimeError(
            "Missing SARA training packages. Install with: "
            "python -m pip install torch transformers datasets peft accelerate safetensors sentencepiece "
            f"\nOriginal import error: {e}"
        )

def choose_base(job, foundation):
    for value in (
        job["base_model_path"],
        foundation["trainable_source_path"] if foundation else "",
        foundation["source_model"] if foundation else "",
    ):
        if value and str(value).strip():
            return str(value).strip()
    raise RuntimeError("No trainable base checkpoint/model ID is configured for this job.")

def load_jsonl_text_dataset(datasets_mod, dataset_path):
    p=Path(dataset_path)
    if not p.exists(): raise RuntimeError(f"Training dataset does not exist: {p}")
    ds=datasets_mod.load_dataset("json", data_files=str(p), split="train")
    def to_text(example):
        inp=str(example.get("input","")).strip()
        out=str(example.get("output","")).strip()
        if not out:
            out=str(example.get("response","")).strip()
        if not inp and "messages" in example:
            return {"text": json.dumps(example["messages"], ensure_ascii=False)}
        if not out:
            raise RuntimeError("Dataset rows must contain output/response text.")
        return {"text": f"User:\n{inp}\n\nAssistant:\n{out}"}
    return ds.map(to_text)

def train_lora(job, foundation, db):
    torch, transformers, datasets_mod, peft = require_training_packages()
    base=choose_base(job, foundation)
    dataset=load_jsonl_text_dataset(datasets_mod, job["dataset_path"])
    output=Path(job["output_path"] or (Path(job["dataset_path"]).parent / f"{job['target_name']}-training"))
    output.mkdir(parents=True, exist_ok=True)
    adapter_dir=output/"adapter"

    update_job(db, job["id"], progress=8)
    tokenizer=transformers.AutoTokenizer.from_pretrained(base, trust_remote_code=True)
    if tokenizer.pad_token is None:
        tokenizer.pad_token=tokenizer.eos_token

    dtype=torch.bfloat16 if torch.cuda.is_available() and getattr(torch.cuda, "is_bf16_supported", lambda: False)() else (
        torch.float16 if torch.cuda.is_available() else torch.float32
    )
    model=transformers.AutoModelForCausalLM.from_pretrained(
        base, torch_dtype=dtype, device_map="auto" if torch.cuda.is_available() else None, trust_remote_code=True
    )
    model.config.use_cache=False

    lora_cfg=peft.LoraConfig(
        r=16,
        lora_alpha=32,
        lora_dropout=0.05,
        bias="none",
        task_type="CAUSAL_LM",
        target_modules=["q_proj","k_proj","v_proj","o_proj","gate_proj","up_proj","down_proj"],
    )
    model=peft.get_peft_model(model,lora_cfg)
    update_job(db, job["id"], progress=18)

    def tokenize(batch):
        enc=tokenizer(batch["text"], truncation=True, max_length=1024, padding=False)
        enc["labels"]=enc["input_ids"].copy()
        return enc
    tokenized=dataset.map(tokenize, batched=True, remove_columns=dataset.column_names)

    args=transformers.TrainingArguments(
        output_dir=str(output/"checkpoints"),
        num_train_epochs=1,
        per_device_train_batch_size=1,
        gradient_accumulation_steps=8,
        learning_rate=2e-4,
        logging_steps=5,
        save_strategy="epoch",
        report_to=[],
        fp16=torch.cuda.is_available() and dtype==torch.float16,
        bf16=torch.cuda.is_available() and dtype==torch.bfloat16,
        remove_unused_columns=False,
    )
    collator=transformers.DataCollatorForLanguageModeling(tokenizer=tokenizer, mlm=False)
    trainer=transformers.Trainer(model=model,args=args,train_dataset=tokenized,data_collator=collator)
    update_job(db, job["id"], progress=25)
    trainer.train()
    update_job(db, job["id"], progress=72)
    model.save_pretrained(adapter_dir, safe_serialization=True)
    tokenizer.save_pretrained(adapter_dir)
    return model, tokenizer, base, output, adapter_dir

def ensure_llama_tools(work_root: Path):
    target=work_root/f"llama.cpp-{LLAMA_TAG}"
    if (target/"convert_lora_to_gguf.py").exists():
        return target
    work_root.mkdir(parents=True, exist_ok=True)
    archive=work_root/f"llama.cpp-{LLAMA_TAG}.zip"
    url=f"https://github.com/ggml-org/llama.cpp/archive/refs/tags/{LLAMA_TAG}.zip"
    print(f"Downloading pinned llama.cpp conversion tools {LLAMA_TAG}...")
    urllib.request.urlretrieve(url, archive)
    with zipfile.ZipFile(archive) as zf:
        zf.extractall(work_root)
    if not (target/"convert_lora_to_gguf.py").exists():
        raise RuntimeError("Pinned llama.cpp conversion tools could not be prepared.")
    return target

def convert_persona_lora(db, job, base, adapter_dir, output):
    tools=ensure_llama_tools(Path(job["output_path"]).parent/"trainer-tools" if job["output_path"] else output/"trainer-tools")
    gguf=output/(job["target_name"].replace(" ","-")+"-LoRA-F16.gguf")
    cmd=[sys.executable,str(tools/"convert_lora_to_gguf.py"),str(adapter_dir),"--outfile",str(gguf),"--outtype","f16"]
    base_path=Path(base)
    if base_path.is_dir():
        cmd.extend(["--base",str(base_path)])
    else:
        cmd.extend(["--base-model-id",base])
    print("Converting persona LoRA to GGUF...")
    subprocess.run(cmd,check=True,cwd=str(tools))
    db.execute("UPDATE persona_lora_bindings SET active=0,updated_utc=CURRENT_TIMESTAMP WHERE persona_name=?",(job["persona_name"],))
    db.execute(
        "INSERT INTO persona_lora_bindings(persona_name,foundation_id,lora_name,lora_path,weight,active) VALUES(?,?,?,?,1.0,1)",
        (job["persona_name"],job["foundation_id"],job["target_name"],str(gguf))
    )
    db.commit()
    return gguf

def complete_persona_lora(db, job, foundation):
    model, tokenizer, base, output, adapter_dir=train_lora(job,foundation,db)
    update_job(db,job["id"],progress=80)
    gguf=convert_persona_lora(db,job,base,adapter_dir,output)
    update_job(db,job["id"],state="COMPLETED",progress=100,error="")
    print(f"Persona LoRA complete: {gguf}")

def complete_foundation(db, job, foundation):
    model, tokenizer, base, output, adapter_dir=train_lora(job,foundation,db)
    update_job(db,job["id"],progress=80)
    print("Merging trained adapter into forked foundation checkpoint...")
    merged=model.merge_and_unload()
    merged_dir=output/"merged-foundation"
    merged.save_pretrained(merged_dir,safe_serialization=True)
    tokenizer.save_pretrained(merged_dir)
    db.execute(
        "UPDATE model_foundations SET trainable_source_path=?,status='CANDIDATE',updated_utc=CURRENT_TIMESTAMP WHERE id=?",
        (str(merged_dir),job["foundation_id"])
    )
    db.commit()
    update_job(db,job["id"],state="COMPLETED",progress=100,error="")
    print(f"Foundation fork checkpoint complete: {merged_dir}")

def complete_correction(db, job):
    src=Path(job["dataset_path"])
    if not src.exists(): raise RuntimeError(f"Correction dataset not found: {src}")
    out=Path(job["output_path"] or src.parent/"prepared-corrections")
    out.mkdir(parents=True,exist_ok=True)
    dst=out/"corrections.jsonl"
    shutil.copy2(src,dst)
    update_job(db,job["id"],state="COMPLETED",progress=100,error="")
    print(f"Correction dataset prepared: {dst}")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--db",required=True)
    ap.add_argument("--job",required=True)
    args=ap.parse_args()
    db=connect(args.db)
    job=load_job(db,args.job)
    try:
        update_job(db,args.job,state="RUNNING",progress=1,error="")
        foundation=load_foundation(db,job["foundation_id"])
        mode=job["training_mode"]
        print(f"Training mode: {mode}")
        if mode=="PERSONA_LORA":
            complete_persona_lora(db,job,foundation)
        elif mode=="FOUNDATION_SFT":
            complete_foundation(db,job,foundation)
        elif mode=="CORRECTION":
            complete_correction(db,job)
        elif mode=="PREFERENCE":
            raise RuntimeError("Preference/DPO weight training is not enabled in this first trainer build yet.")
        else:
            raise RuntimeError(f"Mode {mode} is handled directly inside SARA and does not use the external trainer.")
    except Exception as e:
        traceback.print_exc()
        update_job(db,args.job,state="FAILED",error=str(e))
        raise

if __name__=="__main__":
    main()
