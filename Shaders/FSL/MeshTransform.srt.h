#pragma once

BEGIN_SRT_NO_AB(MeshTransformSrt)
    BEGIN_SRT_SET(PerFrame)
        // Forge 的 SRT 宏在 C++ 侧生成只读描述符，成员名与 shader 绑定一致。
        // NOLINTNEXTLINE(readability-identifier-naming,cppcoreguidelines-avoid-const-or-ref-data-members)
        DECL_CBUFFER(PerFrame, CBUFFER(TransformData), gMeshTransform)
    END_SRT_SET(PerFrame)
END_SRT(MeshTransformSrt)
