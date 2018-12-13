const http = require('http');
const enn = require('./envoy_filter_http_wasm_example.js');
var en = enn();

var reqNo = 0;

server = http.createServer((request, response) => {
    request.on('error', (err) => {
        console.error(err);
        response.statusCode = 400;
        response.end();
    });
    response.on('error', (err) => {
        console.error(err);
    });

    // connect http server
    en["req"] = request;
    en["resp"] = response;

    // invoke filter
    let fresult = en._onStart(reqNo++);
    console.log(fresult);

    request.pipe(response);
});

en.then(function(inst) {
    server.listen(8080);
});