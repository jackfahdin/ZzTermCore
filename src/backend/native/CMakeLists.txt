# native 后端的渲染视图适配：源码位于 src/backend/native/（三级深度），
# 根 CMakeLists 的 src/*/*.cpp GLOB 不覆盖，故经 target_sources 挂入 ZzTermCore。
target_sources(ZzTermCore PRIVATE
    ZzNativeRenderView.cpp
)
