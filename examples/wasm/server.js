const http = require('http');
const enn = require('./envoy_filter_http_wasm_example.js');
var en = enn();

var reqNo = 0;

server = http.createServer((request, response) => {
		let thisReq = reqNo;
		reqNo++;
    request.on('error', (err) => {
        console.error(err);
        response.statusCode = 400;
        response.end();
    });
    response.on('error', (err) => {
        console.error(err);
    });

		response.on('finish', (arg) => {
				console.log(arg);
				en._onDestroy(thisReq);
		});

    // connect http server
    en["req"] = request;
    en["resp"] = response;

    // invoke filter
    let fresult = en._onStart(thisReq);
		if (fresult == 0){ // 0 is Continue
    	console.log(fresult);
    	request.pipe(response);
		} else {
			console.log("suspending request");
			setTimeout(function() {
    			en["req"] = request;
    			en["resp"] = response;
    			fresult = en._onStart(thisReq);
    			request.pipe(response);
			}, 5000);
		}
});

en.then(function(inst) {
    server.listen(8080);
});
