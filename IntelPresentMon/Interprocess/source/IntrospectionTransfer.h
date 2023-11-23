#pragma once
#include <string>
#include <vector>
#include <memory>
#include <span>
#include <ranges>
#include <cstring>
#include "../../PresentMonAPI2/source/PresentMonAPI.h"
#include "../../PresentMonMiddleware/source/ApiHelpers.h"

namespace pmon::ipc::intro
{
	namespace vi = std::views;

	template<typename T>
	concept IsApiClonable = requires {
		typename T::ApiType;
	};

	template<typename T>
	size_t GetPadding(size_t byteIndex)
	{
		constexpr auto alignment = alignof(T);
		const auto partialBytes = byteIndex % alignment;
		const auto padding = (alignment - partialBytes) % alignment;
		return padding;
	}

	template<typename T>
	class ProbeAllocator
	{
		template<typename T2>
		friend class ProbeAllocator;
	public:
		using ProbeTag = std::true_type;
		using value_type = T;
		ProbeAllocator() = default;
		ProbeAllocator(const ProbeAllocator<void>& other)
			: pTotalSize(other.pTotalSize)
		{}
		T* allocate(size_t count)
		{
			*pTotalSize += sizeof(T) * count + GetPadding<T>(*pTotalSize);
			return nullptr;
		}
		void deallocate(T*);
		size_t GetTotalSize() const
		{
			return *pTotalSize;
		}
	private:
		std::shared_ptr<size_t> pTotalSize = std::make_shared<size_t>();
	};

	template<typename T>
	class BlockAllocator
	{
		template<typename T2>
		friend class BlockAllocator;
	public:
		using value_type = T;
		BlockAllocator(size_t nBytes) : pBytes{ reinterpret_cast<char*>(malloc(nBytes)) } {}
		BlockAllocator(const BlockAllocator<void>& other)
			:
			pTotalSize(other.pTotalSize),
			pBytes{ other.pBytes }
		{}
		T* allocate(size_t count)
		{
			*pTotalSize += GetPadding<T>(*pTotalSize);
			const auto pStart = reinterpret_cast<T*>(pBytes + *pTotalSize);
			*pTotalSize += sizeof(T) * count;
			return pStart;
		}
		void deallocate(T*);
	private:
		std::shared_ptr<size_t> pTotalSize = std::make_shared<size_t>();
		char* pBytes = nullptr;
	};

	struct IntrospectionString
	{
		IntrospectionString(std::string s) : buffer_{ std::move(s) } {}
		IntrospectionString& operator=(std::string rhs)
		{
			buffer_ = std::move(rhs);
			return *this;
		}
		using ApiType = PM_INTROSPECTION_STRING;
		template<class V>
		ApiType* ApiClone(V voidAlloc) const
		{
			// local to hold structure contents being built up
			ApiType content;
			// self allocation
			using A = std::allocator_traits<V>::template rebind_alloc<ApiType>;
			A alloc{ voidAlloc };
			auto pSelf = alloc.allocate(1);
			// prepare contents
			using CA = std::allocator_traits<V>::template rebind_alloc<char>;
			CA charAlloc{ voidAlloc };
			const auto bufferSize = buffer_.size() + 1;
			content.pData = charAlloc.allocate(bufferSize);
			if (content.pData) {
				strcpy_s(const_cast<char*>(content.pData), bufferSize, buffer_.c_str());
			}
			// emplace to allocated self
			if (pSelf) {
				std::allocator_traits<A>::construct(alloc, pSelf, content);
			}
			return pSelf;
		}
	private:
		std::string buffer_;
	};

	template<typename T>
	struct IntrospectionObjArray
	{
		IntrospectionObjArray() = default;
		~IntrospectionObjArray()
		{
			for (auto pObj : buffer_) {
				delete pObj;
			}
		}
		void PushBack(std::unique_ptr<T> pObj)
		{
			buffer_.push_back(pObj.release());
		}
		using ApiType = PM_INTROSPECTION_OBJARRAY;
		template<class V>
		ApiType* ApiClone(V voidAlloc) const
		{
			// local to hold structure contents being built up
			ApiType content;
			// self allocation
			using A = std::allocator_traits<V>::template rebind_alloc<ApiType>;
			A alloc{ voidAlloc };
			auto pSelf = alloc.allocate(1);
			// prepare contents
			// allocator to construct pointers inside this container
			using VPA = std::allocator_traits<V>::template rebind_alloc<void*>;
			VPA voidPtrAlloc{ voidAlloc };
			// allocator to construct objects to be pointed to (if not ApiClonable)
			using TA = std::allocator_traits<V>::template rebind_alloc<T>;
			TA tAlloc{ voidAlloc };
			content.size = buffer_.size();
			content.pData = const_cast<const void**>(voidPtrAlloc.allocate(content.size));
			// clone each element from shm to Api struct in heap
			for (size_t i = 0; i < content.size; i++) {
				void* pElement = nullptr;
				if constexpr (IsApiClonable<T>) {
					pElement = buffer_[i]->ApiClone(voidAlloc);
				}
				else {
					auto pNonApiClonableElement = tAlloc.allocate(1);
					if (pNonApiClonableElement) {
						std::allocator_traits<TA>::construct(tAlloc, pNonApiClonableElement, *buffer_[i]);
					}
					pElement = pNonApiClonableElement;
				}
				if (content.pData) {
					content.pData[i] = pElement;
				}
			}
			// emplace to allocated self
			if (pSelf) {
				std::allocator_traits<A>::construct(alloc, pSelf, content);
			}
			return pSelf;
		}
	private:
		std::vector<T*> buffer_;
	};

	struct IntrospectionEnumKey
	{
		IntrospectionEnumKey(PM_ENUM enumId_in, int value_in, std::string symbol_in, std::string name_in, std::string shortName_in, std::string description_in)
			:
			enumId_{ enumId_in},
			value_{ value_in },
			symbol_{ std::move(symbol_in) },
			name_{ std::move(name_in) },
			shortName_{ std::move(shortName_in) },
			description_{ std::move(description_in) }
		{}
		using ApiType = PM_INTROSPECTION_ENUM_KEY;
		template<class V>
		ApiType* ApiClone(V voidAlloc) const
		{
			// local to hold structure contents being built up
			ApiType content;
			// self allocation
			using A = std::allocator_traits<V>::template rebind_alloc<ApiType>;
			A alloc{ voidAlloc };
			auto pSelf = alloc.allocate(1);
			// prepare contents
			content.enumId = enumId_;
			content.value = value_;
			content.pSymbol = symbol_.ApiClone(voidAlloc);
			content.pName = name_.ApiClone(voidAlloc);
			content.pShortName = shortName_.ApiClone(voidAlloc);
			content.pDescription = description_.ApiClone(voidAlloc);
			// emplace to allocated self
			if (pSelf) {
				std::allocator_traits<A>::construct(alloc, pSelf, content);
			}
			return pSelf;
		}
	private:
		PM_ENUM enumId_;
		int value_;
		IntrospectionString symbol_;
		IntrospectionString name_;
		IntrospectionString shortName_;
		IntrospectionString description_;
	};

	struct IntrospectionEnum
	{
		IntrospectionEnum(PM_ENUM id_in, std::string symbol_in, std::string description_in)
			:
			id_{ id_in },
			symbol_{ std::move(symbol_in) },
			description_{ std::move(description_in) }
		{}
		void AddKey(std::unique_ptr<IntrospectionEnumKey> pKey)
		{
			keys_.PushBack(std::move(pKey));
		}
		using ApiType = PM_INTROSPECTION_ENUM;
		template<class V>
		ApiType* ApiClone(V voidAlloc) const
		{
			// local to hold structure contents being built up
			ApiType content;
			// self allocation
			using A = std::allocator_traits<V>::template rebind_alloc<ApiType>;
			A alloc{ voidAlloc };
			auto pSelf = alloc.allocate(1);
			// prepare contents
			content.id = id_;
			content.pSymbol = symbol_.ApiClone(voidAlloc);
			content.pDescription = description_.ApiClone(voidAlloc);
			content.pKeys = keys_.ApiClone(voidAlloc);
			// emplace to allocated self
			if (pSelf) {
				std::allocator_traits<A>::construct(alloc, pSelf, content);
			}
			return pSelf;
		}
	private:
		PM_ENUM id_;
		IntrospectionString symbol_;
		IntrospectionString description_;
		IntrospectionObjArray<IntrospectionEnumKey> keys_;
	};

	struct IntrospectionDevice
	{
		IntrospectionDevice(uint32_t id_in, PM_DEVICE_TYPE type_in, PM_DEVICE_VENDOR vendor_in, std::string name_in)
			:
			id_{ id_in },
			type_{ type_in },
			vendor_{ vendor_in },
			name_{ std::move(name_in) }
		{}
		using ApiType = PM_INTROSPECTION_DEVICE;
		template<class V>
		ApiType* ApiClone(V voidAlloc) const
		{
			// local to hold structure contents being built up
			ApiType content;
			// self allocation
			using A = std::allocator_traits<V>::template rebind_alloc<ApiType>;
			A alloc{ voidAlloc };
			auto pSelf = alloc.allocate(1);
			// prepare contents
			content.id = id_;
			content.type = type_;
			content.vendor = vendor_;
			content.pName = name_.ApiClone(voidAlloc);
			// emplace to allocated self
			if (pSelf) {
				std::allocator_traits<A>::construct(alloc, pSelf, content);
			}
			return pSelf;
		}
	private:
		uint32_t id_;
		PM_DEVICE_TYPE type_;
		PM_DEVICE_VENDOR vendor_;
		IntrospectionString name_;
	};

	struct IntrospectionDeviceMetricInfo
	{
		IntrospectionDeviceMetricInfo(uint32_t deviceId_in, PM_METRIC_AVAILABILITY availability_in, uint32_t arraySize_in)
			:
			deviceId_{ deviceId_in },
			availability_{ availability_in },
			arraySize_{ arraySize_in }
		{}
		using ApiType = PM_INTROSPECTION_DEVICE_METRIC_INFO;
		template<class V>
		ApiType* ApiClone(V voidAlloc) const
		{
			// local to hold structure contents being built up
			ApiType content;
			// self allocation
			using A = std::allocator_traits<V>::template rebind_alloc<ApiType>;
			A alloc{ voidAlloc };
			auto pSelf = alloc.allocate(1);
			// prepare contents
			content.deviceId = deviceId_;
			content.availability = availability_;
			content.arraySize = arraySize_;
			// emplace to allocated self
			if (pSelf) {
				std::allocator_traits<A>::construct(alloc, pSelf, content);
			}
			return pSelf;
		}
	private:
		uint32_t deviceId_;
		PM_METRIC_AVAILABILITY availability_;
		uint32_t arraySize_;
	};

	struct IntrospectionDataTypeInfo
	{
		IntrospectionDataTypeInfo(PM_DATA_TYPE type_in, PM_ENUM enumId_in)
			:
			type_{ type_in },
			enumId_{ enumId_in }
		{}
		using ApiType = PM_INTROSPECTION_DATA_TYPE_INFO;
		template<class V>
		ApiType* ApiClone(V voidAlloc) const
		{
			// local to hold structure contents being built up
			ApiType content;
			// self allocation
			using A = std::allocator_traits<V>::template rebind_alloc<ApiType>;
			A alloc{ voidAlloc };
			auto pSelf = alloc.allocate(1);
			// prepare contents
			content.type = type_;
			content.enumId = enumId_;
			// emplace to allocated self
			if (pSelf) {
				std::allocator_traits<A>::construct(alloc, pSelf, content);
			}
			return pSelf;
		}
	private:
		PM_DATA_TYPE type_;
		PM_ENUM enumId_;
	};

	struct IntrospectionMetric
	{
		IntrospectionMetric(PM_METRIC id_in, PM_METRIC_TYPE type_in, PM_UNIT unit_in, const IntrospectionDataTypeInfo& typeInfo_in, std::vector<PM_STAT> stats_in = {})
			:
			id_{ id_in },
			type_{ type_in },
			unit_{ unit_in },
			pTypeInfo_{ std::make_unique<IntrospectionDataTypeInfo>(typeInfo_in) }
		{
			AddStats(std::move(stats_in));
		}
		void AddStat(PM_STAT stat)
		{
			stats_.PushBack(std::make_unique<PM_STAT>(stat));
		}
		void AddStats(std::vector<PM_STAT> stats)
		{
			for (auto stat : stats) {
				stats_.PushBack(std::make_unique<PM_STAT>(stat));
			}
		}
		void AddDeviceMetricInfo(IntrospectionDeviceMetricInfo info)
		{
			deviceMetricInfo_.PushBack(std::make_unique<IntrospectionDeviceMetricInfo>(info));
		}
		using ApiType = PM_INTROSPECTION_METRIC;
		template<class V>
		ApiType* ApiClone(V voidAlloc) const
		{
			// local to hold structure contents being built up
			ApiType content;
			// self allocation
			using A = std::allocator_traits<V>::template rebind_alloc<ApiType>;
			A alloc{ voidAlloc };
			auto pSelf = alloc.allocate(1);
			// prepare contents
			content.id = id_;
			content.type = type_;
			content.unit = unit_;
			content.pTypeInfo = pTypeInfo_->ApiClone(voidAlloc);
			content.pStats = stats_.ApiClone(voidAlloc);
			content.pDeviceMetricInfo = deviceMetricInfo_.ApiClone(voidAlloc);
			// emplace to allocated self
			if (pSelf) {
				std::allocator_traits<A>::construct(alloc, pSelf, content);
			}
			return pSelf;
		}
		PM_METRIC GetId() const
		{
			return id_;
		}
	private:
		PM_METRIC id_;
		PM_METRIC_TYPE type_;
		PM_UNIT unit_;
		std::unique_ptr<IntrospectionDataTypeInfo> pTypeInfo_;
		IntrospectionObjArray<PM_STAT> stats_;
		IntrospectionObjArray<IntrospectionDeviceMetricInfo> deviceMetricInfo_;
	};

	struct IntrospectionRoot : PM_INTROSPECTION_ROOT
	{
		void AddEnum(std::unique_ptr<IntrospectionEnum> pEnum)
		{
			enums_.PushBack(std::move(pEnum));
		}
		void AddMetric(std::unique_ptr<IntrospectionMetric> pMetric)
		{
			metrics_.PushBack(std::move(pMetric));
		}
		void AddDevice(std::unique_ptr<IntrospectionDevice> pDevice)
		{
			devices_.PushBack(std::move(pDevice));
		}
		using ApiType = PM_INTROSPECTION_ROOT;
		template<class V>
		mid::UniqueApiRootPtr ApiClone(V voidAlloc) const
		{
			// local to hold structure contents being built up
			ApiType content;
			// self allocation
			using A = std::allocator_traits<V>::template rebind_alloc<ApiType>;
			A alloc{ voidAlloc };
			auto pSelf = alloc.allocate(1);
			// TODO: prepare contents
			content.pMetrics = metrics_.ApiClone(voidAlloc);
			content.pEnums = enums_.ApiClone(voidAlloc);
			content.pDevices = devices_.ApiClone(voidAlloc);
			// emplace to allocated self
			if (pSelf) {
				std::allocator_traits<A>::construct(alloc, pSelf, content);
			}
			return mid::UniqueApiRootPtr(pSelf);
		}
	private:
		IntrospectionObjArray<IntrospectionMetric> metrics_;
		IntrospectionObjArray<IntrospectionEnum> enums_;
		IntrospectionObjArray<IntrospectionDevice> devices_;
	};
}