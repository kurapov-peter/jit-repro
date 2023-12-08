# jit-repro

conda env create -f deps.yml  
conda activate repro  
cmake -Bbuild -GNinja .  
cmake --build build

LD_PRELOAD=build/libruntime.so ./build/main input.spv
