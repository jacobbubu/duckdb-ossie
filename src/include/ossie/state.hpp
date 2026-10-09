#pragma once

#include "duckdb/common/mutex.hpp"
#include "duckdb/storage/object_cache.hpp"
#include "ossie/model.hpp"

namespace duckdb {
class ClientContext;

namespace ossie {

//! Published model and policy are captured together; readers never mutate a published model.
struct OssieSnapshot {
	shared_ptr<const Model> model;
	bool allow_filter_functions = false;
};

//! The loaded model, held per database rather than per connection: an MCP server answers on its own
//! connection, and a model loaded by the init script has to be visible there.
class OssieState : public ObjectCacheEntry {
public:
	static constexpr const char *STATE_KEY = "ossie";

	static OssieState &Get(ClientContext &context);

	void SetModel(shared_ptr<Model> new_model, bool allow_filter_functions);

	//! One lock captures both components. Null model until loaded; old readers survive replacement.
	OssieSnapshot GetSnapshot();
	//! Discovery needs only the model; never use this accessor with a separately read policy.
	shared_ptr<const Model> GetModel();

	static string ObjectType() {
		return "ossie_model";
	}
	string GetObjectType() override {
		return ObjectType();
	}
	//! Invalid means non-evictable. An evicted model would look exactly like no model was loaded.
	optional_idx GetEstimatedCacheMemory() const override {
		return optional_idx();
	}

private:
	mutex lock;
	shared_ptr<const Model> model;
	bool allow_filter_functions = false;
};

} // namespace ossie
} // namespace duckdb
