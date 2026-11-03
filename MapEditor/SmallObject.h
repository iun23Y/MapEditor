#pragma once

#include "Object.h"
#include "BlockData.h"
#include "ObjectTypes.h"

class SmallObject : public Object
{
public:
	SmallObject() : Object(ObjectType::SmallObject) {}

	void setData(BlockData&& data) { m_data = std::move(data); }
	const BlockData& data() const { return m_data; }

	Bounds bounds() const override {
		return Bounds{ position + m_data.minCorner(), position + m_data.maxCorner() };
	}
private:
	BlockData m_data;
};

