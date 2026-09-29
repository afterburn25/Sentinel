from __future__ import annotations
import argparse, gc, json, os, shutil, sqlite3, subprocess, sys, threading, traceback, urllib.request, zipfile
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
        if state=="RUNNING":
            fields.append("started_utc=CURRENT_TIMESTAMP")
            fields.append("worker_pid=?"); values.append(os.getpid())
        if state in ("COMPLETED","FAILED","CANCELLED"):
            fields.append("completed_utc=CURRENT_TIMESTAMP")
            fields.append("worker_pid=0")
    if progress is not None:
        fields.append("progress=?"); values.append(int(progress))
    if error is not None:
        fields.append("error_text=?"); values.append(str(error))
    fields.append("heartbeat_utc=CURRENT_TIMESTAMP")
    values.append(job_id)
    db.execute(f"UPDATE trainer_jobs SET {','.join(fields)} WHERE id=?", values)
    db.commit()

def heartbeat_worker(db_path: str, job_id: str, stop_event: threading.Event):
    heartbeat_db=connect(db_path)
    try:
        while not stop_event.wait(30):
            heartbeat_db.execute(
                "UPDATE trainer_jobs SET heartbeat_utc=CURRENT_TIMESTAMP "
                "WHERE id=? AND state='RUNNING'",
                (job_id,),
            )
            heartbeat_db.commit()
    finally:
        heartbeat_db.close()

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
        import bitsandbytes
        return torch, transformers, datasets, peft, bitsandbytes
    except Exception as e:
        raise RuntimeError(
            "Missing SARA training packages. Use Prepare Env in Model Lab / Train. "
            f"\nOriginal import error: {e}"
        )

def load_trainable_text_model(torch, transformers, peft, base):
    if not torch.cuda.is_available():
        raise RuntimeError(
            "SARA 9B LoRA/foundation weight training requires an NVIDIA CUDA GPU. "
            "Behavior Tuning and Dataset Training remain available without CUDA."
        )

    props=torch.cuda.get_device_properties(0)
    vram_gb=props.total_memory/(1024**3)
    print(f"Training GPU: {props.name} ({vram_gb:.1f} GB VRAM)")
    if vram_gb < 10.0:
        raise RuntimeError(
            f"Only {vram_gb:.1f} GB VRAM is available. "
            "SARA's 9B QLoRA profile requires approximately 10 GB or more."
        )

    compute_dtype=(
        torch.bfloat16
        if getattr(torch.cuda, "is_bf16_supported", lambda: False)()
        else torch.float16
    )
    quant=transformers.BitsAndBytesConfig(
        load_in_4bit=True,
        bnb_4bit_quant_type="nf4",
        bnb_4bit_use_double_quant=True,
        bnb_4bit_compute_dtype=compute_dtype,
    )
    config=transformers.AutoConfig.from_pretrained(base, trust_remote_code=True)
    kwargs=dict(
        quantization_config=quant,
        device_map="auto",
        trust_remote_code=True,
    )

    if getattr(config,"model_type","")=="qwen3_5":
        try:
            from transformers.models.qwen3_5.modeling_qwen3_5 import Qwen3_5ForCausalLM
            model=Qwen3_5ForCausalLM.from_pretrained(base, **kwargs)
        except Exception as e:
            raise RuntimeError(
                "The trainer could not load the Qwen3.5 text backbone. "
                "Run Prepare Env again to update the SARA trainer environment. "
                f"Original error: {e}"
            ) from e
    else:
        model=transformers.AutoModelForCausalLM.from_pretrained(base, **kwargs)

    model=peft.prepare_model_for_kbit_training(model)
    if hasattr(model,"gradient_checkpointing_enable"):
        model.gradient_checkpointing_enable()
    model.config.use_cache=False
    return model, compute_dtype

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
    torch, transformers, datasets_mod, peft, _bitsandbytes = require_training_packages()
    base=choose_base(job, foundation)
    dataset=load_jsonl_text_dataset(datasets_mod, job["dataset_path"])
    output=Path(job["output_path"] or (Path(job["dataset_path"]).parent / f"{job['target_name']}-training"))
    output.mkdir(parents=True, exist_ok=True)
    adapter_dir=output/"adapter"

    update_job(db, job["id"], progress=8)
    tokenizer=transformers.AutoTokenizer.from_pretrained(base, trust_remote_code=True)
    if tokenizer.pad_token is None:
        tokenizer.pad_token=tokenizer.eos_token

    model, dtype=load_trainable_text_model(torch,transformers,peft,base)

    lora_cfg=peft.LoraConfig(
        r=16,
        lora_alpha=32,
        lora_dropout=0.05,
        bias="none",
        task_type="CAUSAL_LM",
        target_modules="all-linear",
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
        fp16=dtype==torch.float16,
        bf16=dtype==torch.bfloat16,
        gradient_checkpointing=True,
        optim="paged_adamw_8bit",
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

    # Training completion must never mutate the live persona runtime. Register
    # the artifact as an inactive candidate; the investigator must explicitly
    # review/evaluate and activate a version from Model Lab.
    db.execute(
        "INSERT INTO persona_lora_bindings(persona_name,foundation_id,lora_name,lora_path,weight,active) VALUES(?,?,?,?,1.0,0)",
        (job["persona_name"],job["foundation_id"],job["target_name"],str(gguf))
    )
    db.commit()
    return gguf

def complete_persona_lora(db, job, foundation):
    model, tokenizer, base, output, adapter_dir=train_lora(job,foundation,db)
    update_job(db,job["id"],progress=80)
    gguf=convert_persona_lora(db,job,base,adapter_dir,output)
    update_job(db,job["id"],state="COMPLETED",progress=100,error="")
    print(f"Persona LoRA candidate complete: {gguf}")
    print("Candidate remains inactive until explicitly reviewed/evaluated and activated in SARA Model Lab.")

def find_llama_quantizer(app_root: Path):
    for root in (app_root/"ai"/"runtime", app_root/"ai"/"runtime_cpu"):
        if not root.exists():
            continue
        for name in ("llama-quantize.exe","quantize.exe"):
            matches=list(root.rglob(name))
            if matches:
                return matches[0]
    raise RuntimeError(
        "llama.cpp quantizer was not found in the installed SARA AI runtime. "
        "Use Repair Local AI, then rerun the foundation job."
    )

def build_foundation_runtime(job, merged_dir: Path, output: Path, app_root: Path):
    tools=ensure_llama_tools(output/"trainer-tools")
    converter=tools/"convert_hf_to_gguf.py"
    if not converter.exists():
        raise RuntimeError("Pinned llama.cpp HF-to-GGUF converter is missing.")

    f16=output/(job["target_name"].replace(" ","-")+"-F16.gguf")
    q4=output/(job["target_name"].replace(" ","-")+"-Q4_K_M.gguf")

    print("Converting merged foundation checkpoint to GGUF...")
    subprocess.run(
        [sys.executable,str(converter),str(merged_dir),"--outfile",str(f16),"--outtype","f16"],
        check=True,cwd=str(tools)
    )

    quantizer=find_llama_quantizer(app_root)
    print(f"Quantizing foundation runtime with {quantizer.name}...")
    subprocess.run([str(quantizer),str(f16),str(q4),"Q4_K_M"],check=True)
    try:
        f16.unlink()
    except OSError:
        pass
    if not q4.exists():
        raise RuntimeError("Foundation GGUF quantization completed without producing the expected runtime file.")
    return q4

def complete_foundation(db, job, foundation, app_root):
    model, tokenizer, base, output, adapter_dir=train_lora(job,foundation,db)
    update_job(db,job["id"],progress=76)

    # Foundation Fork must become a standalone model. Do not merge the LoRA
    # into the 4-bit QLoRA training model: PEFT explicitly notes that some
    # quantization settings are not safe merge targets. Free the quantized
    # training model, reload the base checkpoint at its native dtype on CPU,
    # then merge the saved adapter into that full checkpoint.
    torch, transformers, _datasets, peft, _bitsandbytes=require_training_packages()
    del model
    gc.collect()
    if torch.cuda.is_available():
        torch.cuda.empty_cache()

    print("Reloading base checkpoint on CPU for safe foundation merge...")
    update_job(db,job["id"],progress=80)
    full_base=transformers.AutoModelForCausalLM.from_pretrained(
        base,
        dtype="auto",
        device_map={"": "cpu"},
        low_cpu_mem_usage=True,
        trust_remote_code=True,
    )
    merge_model=peft.PeftModel.from_pretrained(full_base,adapter_dir,is_trainable=False)

    print("Merging trained adapter into full-precision/native-dtype foundation checkpoint...")
    merged=merge_model.merge_and_unload(safe_merge=True)
    merged_dir=output/"merged-foundation"
    merged.save_pretrained(
        merged_dir,
        safe_serialization=True,
        max_shard_size="5GB",
    )
    tokenizer.save_pretrained(merged_dir)

    del merge_model
    del full_base
    del merged
    gc.collect()

    update_job(db,job["id"],progress=88)
    runtime_gguf=build_foundation_runtime(job,merged_dir,output,app_root)
    update_job(db,job["id"],progress=97)

    db.execute(
        "UPDATE model_foundations "
        "SET trainable_source_path=?,runtime_gguf_path=?,status='CANDIDATE',updated_utc=CURRENT_TIMESTAMP "
        "WHERE id=?",
        (str(merged_dir),str(runtime_gguf),job["foundation_id"])
    )
    db.commit()
    update_job(db,job["id"],state="COMPLETED",progress=100,error="")
    print(f"Foundation fork checkpoint complete: {merged_dir}")
    print(f"Foundation runtime GGUF: {runtime_gguf}")

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
    ap.add_argument("--app-root",required=True)
    args=ap.parse_args()
    db=connect(args.db)
    job=load_job(db,args.job)
    heartbeat_stop=threading.Event()
    heartbeat_thread=None
    try:
        update_job(db,args.job,state="RUNNING",progress=1,error="")
        heartbeat_thread=threading.Thread(
            target=heartbeat_worker,
            args=(args.db,args.job,heartbeat_stop),
            name=f"sara-trainer-heartbeat-{args.job}",
            daemon=True,
        )
        heartbeat_thread.start()

        foundation=load_foundation(db,job["foundation_id"])
        mode=job["training_mode"]
        print(f"Training mode: {mode}")
        if mode=="PERSONA_LORA":
            complete_persona_lora(db,job,foundation)
        elif mode=="FOUNDATION_SFT":
            complete_foundation(db,job,foundation,Path(args.app_root))
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
    finally:
        heartbeat_stop.set()
        if heartbeat_thread is not None:
            heartbeat_thread.join(timeout=2)

if __name__=="__main__":
    main()
