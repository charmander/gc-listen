#define NAPI_VERSION 5
#include <node_api.h>
#include <uv.h>

#include <stdio.h>
#include <stdlib.h>

#define FAIL_AND_ABORT(message) \
	{ \
		fputs(message "\n", stderr); \
		abort(); \
	}

struct finalize_loop_callback_data {
	napi_env env;
	napi_ref callback_ref;
};

static void finalize_close_callback(uv_handle_t* const idle) {
	free(idle);
}

static void finalize_loop_callback(uv_idle_t* const idle) {
	if (uv_idle_stop(idle) < 0) {
		FAIL_AND_ABORT("uv_idle_stop failed")
	}

	struct finalize_loop_callback_data* const data = uv_handle_get_data((uv_handle_t*)idle);
	uv_close((uv_handle_t*)idle, finalize_close_callback);

	napi_env const env = data->env;
	napi_ref const callback_ref = data->callback_ref;
	free(data);

	napi_handle_scope scope;

	if (napi_open_handle_scope(env, &scope) != napi_ok) {
		FAIL_AND_ABORT("napi_open_handle_scope failed")
	}

	napi_value callback = NULL;

	if (napi_get_reference_value(env, callback_ref, &callback) != napi_ok || callback == NULL) {
		FAIL_AND_ABORT("napi_get_reference_value failed")
	}

	if (napi_delete_reference(env, callback_ref) != napi_ok) {
		FAIL_AND_ABORT("napi_delete_reference failed")
	}

	napi_value undefined;

	if (napi_get_undefined(env, &undefined) != napi_ok) {
		FAIL_AND_ABORT("napi_get_undefined failed")
	}

	napi_status const status = napi_call_function(env, undefined, callback, 0, NULL, NULL);

	if (status != napi_ok) {
		if (status == napi_pending_exception) {
			napi_value exc;

			if (napi_get_and_clear_last_exception(env, &exc) != napi_ok) {
				FAIL_AND_ABORT("napi_get_and_clear_last_exception failed")
			}

			if (napi_fatal_exception(env, exc) != napi_ok) {
				FAIL_AND_ABORT("napi_fatal_exception failed")
			}
		} else {
			FAIL_AND_ABORT("napi_call_function failed")
		}
	}

	if (napi_close_handle_scope(env, scope) != napi_ok) {
		FAIL_AND_ABORT("napi_close_handle_scope failed")
	}
}

static void finalize_gc_callback(napi_env const env, void* const finalize_data, void* finalize_hint) {
	uv_loop_t* loop;

	if (napi_get_uv_event_loop(env, &loop) != napi_ok) {
		FAIL_AND_ABORT("napi_get_uv_event_loop failed")
	}

	uv_idle_t* const idle = malloc(sizeof(uv_idle_t));

	if (idle == NULL) {
		FAIL_AND_ABORT("Failed to allocate memory for libuv idle handle")
	}

	if (uv_idle_init(loop, idle) < 0) {
		FAIL_AND_ABORT("uv_idle_init failed")
	}

	struct finalize_loop_callback_data* const data = malloc(sizeof(struct finalize_loop_callback_data));

	if (data == NULL) {
		FAIL_AND_ABORT("Failed to allocate memory for finalize loop callback data")
	}

	data->env = env;
	data->callback_ref = finalize_hint;

	uv_handle_set_data((uv_handle_t*)idle, data);

	if (uv_idle_start(idle, finalize_loop_callback) < 0) {
		FAIL_AND_ABORT("uv_idle_start failed")
	}
}

static napi_value gc_listen(napi_env const env, napi_callback_info const cbinfo) {
	napi_value argv[2];
	size_t argc = 2;

	if (napi_get_cb_info(env, cbinfo, &argc, argv, NULL, NULL) != napi_ok) {
		napi_throw_error(env, NULL, "napi_get_cb_info failed");
		return NULL;
	}

	{
		napi_valuetype type;

		if (argc < 2 || napi_typeof(env, argv[1], &type) != napi_ok || type != napi_function) {
			napi_throw_type_error(env, NULL, "Garbage collection callback must be a function");
			return NULL;
		}
	}

	napi_ref callback_ref;

	if (napi_create_reference(env, argv[1], 1, &callback_ref) != napi_ok) {
		napi_throw_error(env, NULL, "napi_create_reference failed");
		return NULL;
	}

	if (napi_add_finalizer(env, argv[0], NULL, finalize_gc_callback, callback_ref, NULL) != napi_ok) {
		napi_throw_error(env, NULL, "Failed to add finalizer to value");
		return NULL;
	}

	return NULL;
}

static napi_value init(napi_env const env, napi_value const exports) {
	napi_value export;

	if (napi_create_function(env, "gc_listen", NAPI_AUTO_LENGTH, gc_listen, NULL, &export) != napi_ok) {
		return NULL;
	}

	return export;
}

NAPI_MODULE(gc_listen, init)
