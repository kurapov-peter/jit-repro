declare ptr @gpuCreateStream(ptr noundef %0, ptr noundef %1)
declare ptr @gpuModuleLoad(ptr noundef %0, ptr noundef %1, i64 noundef %2)
declare void @gpuStreamDestroy(ptr noundef %0)

define void @vadd_entry(ptr %A, ptr %B, ptr %C, i64 %Size, ptr %spirv, i64 %spirv.size) {
    %device = alloca ptr, align 8
    %context = alloca ptr, align 8
    store ptr null, ptr %device, align 8
    store ptr null, ptr %context, align 8
    %1 = call ptr @gpuCreateStream(ptr %device, ptr %context)
    %2 = call ptr @gpuModuleLoad(ptr %1, ptr %spirv, i64 %spirv.size)
    call void @gpuStreamDestroy(ptr %1)
    ret void
}