mergeInto(LibraryManager.library, {
    envoy_log: function(arg1, arg2) {
        ps = Pointer_stringify(arg2);
        console.log("envoy_log:", arg1, ps);
    },
    envoy_addHeader: function(type, key_data, key_size, value_data, value_size) {
        key = UTF8ToString(key_data); //, key_size);
        value = UTF8ToString(value_data); //, key_size);
        ah = Module["add_header"]
			  if (ah) {
					ah(key, value);
				}
		},
    envoy_getHeader: function(type, key_data, key_size, value_ptr, value_size_ptr) {
        key = UTF8ToString(key_data); //, key_size);
        rh = Module["request_headers"]();

        let val = rh[key];
        let val_len = val.length;

        let string_on_heap = _malloc(val_len + 1);
        stringToUTF8(val, string_on_heap, val_len + 1);
        setValue(value_ptr, string_on_heap, "i32");
        setValue(value_size_ptr, val_len + 1, "i32");
    },
    envoy_replaceHeader: function() {},
    envoy_removeHeader: function() {},
    envoy_getBodyBufferBytes: function() {},
    envoy_getHeaderPairs: function(headertype, hptr, hptr_size) {
        rh = Module["request_headers"]();
        let size = 4; // size of int32
        let nkeys = 0;
        Object.keys(rh).forEach(key => {
            size += 8; // size of key, size of value
            size += key.length + 1; // null terminated key
            size += rh[key].length + 1; // null terminated value
            nkeys += 1;
        });
        let ptr = _malloc(size);
        let curr = ptr;


        setValue(curr, nkeys, "i32");
        curr += 4; // sizeof(i32)
        Object.keys(rh).forEach(key => {
            setValue(curr, key.length, "i32");
            curr += 4; // sizeof(i32)
            setValue(curr, rh[key].length, "i32");
            curr += 4; // sizeof(i32)
        });

        Object.keys(rh).forEach(key => {
            stringToUTF8(key, curr, key.length + 1);
            curr += key.length + 1;
            let val = rh[key];
            stringToUTF8(val, curr, val.length + 1);
            curr += val.length + 1;
        });

        setValue(hptr, ptr, "i32");
        setValue(hptr_size, size, "i32");
    },
});
