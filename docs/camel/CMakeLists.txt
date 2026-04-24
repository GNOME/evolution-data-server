configure_file(camel.toml.in camel.toml @ONLY)
generate_gi_documentation(
	camel
	${CMAKE_CURRENT_BINARY_DIR}/camel.toml
	"${CAMEL_GIR_PATH}/Camel-${API_VERSION}.gir"
)