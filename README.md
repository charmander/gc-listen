## gc-listen

A way to attach a JavaScript function with `napi_add_finalizer`. For debugging.

```javascript
const gcListen = require('gc-listen');

const obj = {};

const callback = () => {
    // …
};

gcListen(obj, callback);
```

`callback` won’t be called as long as `obj` isn’t eligible for garbage collection. N-API even suggests it will be called *when* `obj` is collected.
