set(METAMOD_DIR "${PROJECT_SOURCE_DIR}/third_party/metamod-source" CACHE PATH "Metamod:Source checkout")

# Metamod supplies KHook at runtime; only its headers are needed here.
add_library(acceleratorlocal_metamod INTERFACE)
target_compile_definitions(acceleratorlocal_metamod INTERFACE META_IS_SOURCE2)
target_include_directories(acceleratorlocal_metamod INTERFACE
	"${METAMOD_DIR}/core"
	"${METAMOD_DIR}/third_party/khook/include"
)
