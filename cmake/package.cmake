set(PLUGIN_NAME acceleratorlocal)
set(PLUGIN_BINARY_DIR "addons/${PLUGIN_NAME}/bin/linuxsteamrt64")
set(PACKAGE_ROOT "${PROJECT_BINARY_DIR}/package")

set_target_properties(AcceleratorLocal PROPERTIES
	OUTPUT_NAME "${PLUGIN_NAME}"
	LIBRARY_OUTPUT_DIRECTORY "${PACKAGE_ROOT}/${PLUGIN_BINARY_DIR}"
)

configure_file("${PROJECT_SOURCE_DIR}/acceleratorlocal.ini"
	"${PACKAGE_ROOT}/addons/${PLUGIN_NAME}/acceleratorlocal.ini" COPYONLY
)

file(MAKE_DIRECTORY "${PACKAGE_ROOT}/addons/metamod")
file(CONFIGURE
	OUTPUT "${PACKAGE_ROOT}/addons/metamod/${PLUGIN_NAME}.vdf"
	CONTENT [=[
"Metamod Plugin"
{
	"alias"	"@PLUGIN_NAME@"
	"file"	"@PLUGIN_BINARY_DIR@/@PLUGIN_NAME@"
}
]=]
	@ONLY
	NEWLINE_STYLE LF
)
