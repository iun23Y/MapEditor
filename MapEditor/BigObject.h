#pragma once

#include "Object.h"
#include "ChunkData.h"
#include "ObjectTypes.h"

class BigObject : public Object
{
public:
	BigObject(ObjectID id) : Object(id, ObjectType::BigObject) {}

	void setData(ChunkData&& data) { m_data = std::move(data); }
	const ChunkData& data() const { return m_data; }

	Bounds bounds() const override {
		if (m_data.empty()) return Bounds{ position, position };
		return Bounds{ position + m_data.minPos(),
					   position + m_data.maxPos() };
	}
private:
	ChunkData m_data;
};
