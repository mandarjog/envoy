const enn = require('./envoy_filter_http_wasm_example.js');
var en = enn();

// library uses request_headers function which can be injected here
// by the user. There must be a better way !
en["request_headers"] = function() {
    return {
        "qq": "gg",
        "ab": "cc",
        "woo": "hoo"
    };
}

en.then(function(inst) {
    inst._onStart();
});