#include "Sentinel/Evidence/SevContainer.hpp"
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){if(argc!=2){std::cerr<<"Usage: SentinelEvidenceVerifier <file.sev>\n";return 2;}auto p=std::filesystem::path(argv[1]);bool ok=sentinel::SevContainer::BasicValidate(p);std::cout<<"Sentinel Evidence Verification\nContainer: "<<(ok?"STRUCTURALLY VALID":"INVALID")<<"\n";return ok?0:1;}
