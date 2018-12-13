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
	en["request_headers"] = function() {
		return request.headers;
	};

	en["add_header"] = function(key, val) {
		response.setHeader(key, val)
	}

 	// invoke filter
	let fresult = en._onStart(reqNo++);
	console.log (fresult);

  if (request.method === 'POST' && request.url === '/echo') {
    request.pipe(response);
  } else {
    response.statusCode = 404;
    response.end();
  }
});
	
en.then(function(inst) {
		server.listen(8080);
});
