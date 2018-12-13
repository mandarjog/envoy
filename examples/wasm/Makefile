#OPT=-O3
OPT=-s EXTRA_EXPORTED_RUNTIME_METHODS='["addOnPostRun"]' -s EXPORTED_FUNCTIONS='["_onStart"]' -s DEMANGLE_SUPPORT=1 -s MODULARIZE=1

run: envoy_filter_http_wasm_example.wasm runfilter.js
	node runfilter.js

envoy_filter_http_wasm_example.wasm: envoy_filter_http_wasm_example.cc envoy_wasm_intrinsics.h envoy_wasm_filter_intrinsics.h envoy_wasm_intrinsics.js
	em++ -s WASM=1 --std=c++14 $(OPT) -g3 --js-library envoy_wasm_intrinsics.js envoy_filter_http_wasm_example.cc -o envoy_filter_http_wasm_example.js
	#em++ -s WASM=1 --std=c++14 $(OPT) -g3 --pre-js enn.js --js-library envoy_wasm_intrinsics.js envoy_filter_http_wasm_example.cc -o envoy_filter_http_wasm_example.js

