#include <cuda.h>
#include <cuda_runtime.h>

#include "Tensors/Storage.h"
#include "Memory/MemoryManager.h"
#include "Tensors/Cuda/CudaErrorCheck.h"

#define ENABLE_STORAGE_LOGS

#ifdef ENABLE_STORAGE_LOGS
#include <windows.h>
#include <debugapi.h>
#define STORAGE_DEBUG_INFO(...) do { static char buffer[1024]; sprintf(buffer, __VA_ARGS__); OutputDebugString(buffer); } while(0)
#else
#define STORAGE_DEBUG_INFO(...) {}
#endif

namespace Neuro
{
    //////////////////////////////////////////////////////////////////////////
    Storage::Storage(int type, size_t size, const string& name)
        : m_Type(type), m_AllocSize(size), m_Size(size), m_Name(name), m_DataLocation(None)
    {
        if (m_Type & ST_Offloadable)
        {
            CUDA_CHECK(cudaEventCreate(&m_OffloadEvent));
            CUDA_CHECK(cudaEventCreate(&m_PrefetchEvent));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    Storage::Storage(const Storage& other)
    {
        *this = other;
    }

    //////////////////////////////////////////////////////////////////////////
    Storage::Storage(Storage&& other)
    {
        *this = move(other);
    }

    //////////////////////////////////////////////////////////////////////////
    Storage& Storage::operator=(const Storage& other)
    {
        if (this != &other)
        {
            m_AllocSize = other.m_AllocSize;
            m_Size = other.m_Size;
            m_DeviceDataRefCount = 0;
            m_Name = other.m_Name;
            FreeOnHost();
            FreeOnDevice();
            ChangeType(other.m_Type);
            if (other.m_DataPtr)
            {
                NEURO_ASSERT(other.m_DataLocation != None, "");
                m_DataLocation = Host;
                AllocateOnHost();
                if (other.m_DataLocation == Host)
                    memcpy(m_DataPtr, other.m_DataPtr, SizeInBytes());
                else
                    cudaMemcpy(m_DataPtr, other.m_DeviceDataPtr, SizeInBytes(), cudaMemcpyDeviceToHost);
            }
            else
            {
                m_DataLocation = None;
                m_DataPtr = nullptr;
            }
            m_DeviceDataPtr = nullptr;            
        }
        return *this;
    }

    //////////////////////////////////////////////////////////////////////////
    Storage& Storage::operator=(Storage&& other)
    {
        if (this != &other)
        {
            m_AllocSize = other.m_AllocSize;
            m_Size = other.m_Size;
            m_DeviceDataRefCount = other.m_DeviceDataRefCount;
            m_Name = other.m_Name;
            m_DataLocation = other.m_DataLocation;
            m_DataPtr = other.m_DataPtr;
            other.m_DataPtr = nullptr;
            m_DeviceDataPtr = other.m_DeviceDataPtr;
            other.m_DeviceDataPtr = nullptr;
        }
        return *this;
    }

    //////////////////////////////////////////////////////////////////////////
    Storage::~Storage()
    {
        FreeOnDevice();
        FreeOnHost();
        if (m_OffloadEvent)
            CUDA_CHECK(cudaEventDestroy(m_OffloadEvent));
        if (m_PrefetchEvent)
            CUDA_CHECK(cudaEventDestroy(m_PrefetchEvent));
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::ChangeType(int type)
    {
        if (m_Type == type)
            return;

        NEURO_ASSERT(!m_DataPtr && !m_DeviceDataPtr, "Changing type of allocated storage is not allowed.");

        if ((m_Type & ST_Offloadable) && !(type & ST_Offloadable))
        {
            CUDA_CHECK(cudaEventDestroy(m_OffloadEvent));
            CUDA_CHECK(cudaEventDestroy(m_PrefetchEvent));
        }
        else if (!(m_Type & ST_Offloadable) && (type & ST_Offloadable))
        {
            CUDA_CHECK(cudaEventCreate(&m_OffloadEvent));
            CUDA_CHECK(cudaEventCreate(&m_PrefetchEvent));
        }

        m_Type = type;
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::Resize(size_t size)
    {
        if (size < m_AllocSize)
        {
            m_Size = size;
            return;
        }

        m_AllocSize = m_Size = size;

        if (m_DeviceDataPtr)
        {
            FreeOnDevice();
            AllocateOnDevice();
        }

        if (m_DataPtr)
        {
            FreeOnHost();
            AllocateOnHost();
            m_DataLocation = Host;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::Release()
    {
        FreeOnDevice();
        FreeOnHost();
        m_DataLocation = None;
        m_DeviceDataRefCount = 0;
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::AllocateOnHost() const
    {
        if (m_AllocSize == 0)
            return;

        NEURO_ASSERT(!m_DeviceDataPtr, "");
        STORAGE_DEBUG_INFO("Allocating on host '%s' ", m_Name.c_str());
        if (m_DataPtr)
        {
            STORAGE_DEBUG_INFO("<<< already allocated.\n");
            return;
        }
        STORAGE_DEBUG_INFO("<<< allocating.\n");
        if (m_Type & ST_Offloadable)
            MemoryManager::Default().AllocateHostPinned((void**)&m_DataPtr, AllocSizeInBytes(), m_Name);
        else
            MemoryManager::Default().AllocateHost((void**)&m_DataPtr, AllocSizeInBytes(), m_Name);

        m_DataLocation = Host;
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::FreeOnHost()
    {
        NEURO_ASSERT(!m_DeviceDataPtr, "Data cannot be only on device.");
        STORAGE_DEBUG_INFO("Releasing on host '%s' ", m_Name.c_str());
        if (!m_DataPtr)
        {
            STORAGE_DEBUG_INFO("<<< not allocated.\n");
            return;
        }
        STORAGE_DEBUG_INFO("<<< release incoming.\n");
        if (m_Type & ST_Offloadable)
            MemoryManager::Default().ReleaseHostPinned(m_DataPtr);
        else
            MemoryManager::Default().ReleaseHost(m_DataPtr);
        
        m_DataPtr = nullptr;
        m_DataLocation = None;
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::AllocateOnDevice() const
    {
        if (m_AllocSize == 0)
            return;

        if (!m_DataPtr)
            AllocateOnHost();
        NEURO_ASSERT(m_DataPtr, "Data cannot be only on device.");
        STORAGE_DEBUG_INFO("Allocating on device '%s' ", m_Name.c_str());
        if (m_DeviceDataPtr)
        {
            STORAGE_DEBUG_INFO("<<< already allocated.\n");
            return;
        }
        STORAGE_DEBUG_INFO("<<< allocating.\n");
        CUDA_CHECK(MemoryManager::Default().AllocateDevice((void**)&m_DeviceDataPtr, AllocSizeInBytes(), m_Name));
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::FreeOnDevice()
    {
        STORAGE_DEBUG_INFO("Releasing on device '%s' ", m_Name.c_str());
        if (!m_DeviceDataPtr)
        {
            STORAGE_DEBUG_INFO("<<< not allocated.\n");
            return;
        }

        if (m_Type & ST_KeepDevMem)
        {
            STORAGE_DEBUG_INFO("<<< not allowed.\n");
            return;
        }

        STORAGE_DEBUG_INFO("<<< release incoming.\n");
        MemoryManager::Default().WaitForMemEvent(m_OffloadEvent);
        CUDA_CHECK(MemoryManager::Default().ReleaseDevice((void*)m_DeviceDataPtr));
        m_DeviceDataPtr = nullptr;

        // at this point the only place where values are stored is host memory
        if (m_DataPtr)
            m_DataLocation = Host;
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::Offload() const
    {
        NEURO_ASSERT(m_DataPtr, "Attempting to offload to deallocated host storage.");
        if (!m_AllocSize)
            return;

        STORAGE_DEBUG_INFO("Offloading '%s'[%d] ", m_Name.c_str(), m_Type);
        if (m_Type & ST_Offloadable)
        {
            if (!m_DeviceDataPtr)
            {
                STORAGE_DEBUG_INFO("<<< nothing to offload.\n");
                return;
            }
            STORAGE_DEBUG_INFO("<<< requested.\n");
            CUDA_CHECK(MemoryManager::Default().Offload((void*)m_DataPtr, (void*)m_DeviceDataPtr, SizeInBytes(), m_OffloadEvent));
        }
        else
            STORAGE_DEBUG_INFO("<<< not supported.\n");
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::Prefetch() const
    {
        if (!m_AllocSize)
            return;

        if (m_Type & ST_Offloadable)
        {
            NEURO_ASSERT(m_DataPtr, "Attempting to preload from deallocated host storage.");
            
            if (!m_DeviceDataPtr)
                AllocateOnDevice();

            if (cudaEventQuery(m_PrefetchEvent) != cudaSuccess)
            {
                STORAGE_DEBUG_INFO("Prefetching '%s'[%d] <<< requested already.\n", m_Name.c_str(), m_Type);
            }
            else
            {
                STORAGE_DEBUG_INFO("Prefetching '%s'[%d] <<< requested.\n", m_Name.c_str(), m_Type);
                CUDA_CHECK(MemoryManager::Default().Prefetch((void*)m_DeviceDataPtr, (void*)m_DataPtr, SizeInBytes(), m_PrefetchEvent));
            }
        }
        else
            STORAGE_DEBUG_INFO("Prefetching '%s'[%d] <<< not supported.\n", m_Name.c_str(), m_Type);
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::CopyToDevice() const
    {
        if (m_DataLocation == Device)
            return;

        NEURO_ASSERT(m_DataLocation == Host, "Attempting to copy from unallocated host memory to device.");

        bool wasDevMemoryAllocated = true;

        if (!m_DeviceDataPtr)
        {
            AllocateOnDevice();
            wasDevMemoryAllocated = false;
        }

        if ((m_Type & ST_Offloadable) && wasDevMemoryAllocated)
        {
            STORAGE_DEBUG_INFO("Copy to device '%s'[%d] <<< prefetch completed check only\n", m_Name.c_str(), m_Type);
            // we don't have to wait for offload because a valid copy still exists in GPU memory
            /*if (cudaEventQuery(m_OffloadEvent) == cudaErrorNotReady)
                CUDA_VAR_DEBUG_INFO("Waiting for offload... '%s'[%d]\n", m_Name.c_str(), m_Type);
            MemoryManager::Default().WaitForMemEvent(m_OffloadEvent);*/
            // we have to make sure copy to device is blocked until prefetch is completed
            if (cudaEventQuery(m_PrefetchEvent) == cudaErrorNotReady)
                STORAGE_DEBUG_INFO("Waiting for prefetch... '%s'[%d]\n", m_Name.c_str(), m_Type);
            MemoryManager::Default().WaitForMemEvent(m_PrefetchEvent);
        }
        else
        {
            STORAGE_DEBUG_INFO("Copy to device '%s'[%d]\n", m_Name.c_str(), m_Type);
            CUDA_CHECK(cudaMemcpy((void*)m_DeviceDataPtr, (void*)m_DataPtr, SizeInBytes(), cudaMemcpyHostToDevice));
        }

        m_DataLocation = Device;
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::CopyToHost() const
    {
        if (m_DataLocation == Host)
            return;

        NEURO_ASSERT(m_DataLocation != None, "Attempting to copy to unallocated host memory");

        STORAGE_DEBUG_INFO("Copy to host '%s'[%d]\n", m_Name.c_str(), m_Type);
        if (m_Type & ST_Offloadable)
        {
            if (cudaEventQuery(m_OffloadEvent) == cudaErrorNotReady)
                STORAGE_DEBUG_INFO("Waiting for offload... '%s'[%d]\n", m_Name.c_str(), m_Type);
            MemoryManager::Default().WaitForMemEvent(m_OffloadEvent);
        }
        else
            CUDA_CHECK(cudaMemcpy((void*)m_DataPtr, (void*)m_DeviceDataPtr, SizeInBytes(), cudaMemcpyDeviceToHost));

        m_DataLocation = Host;
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::OverrideHost()
    {
        if (!m_DataPtr)
            AllocateOnHost();
        m_DataLocation = Host;
        STORAGE_DEBUG_INFO("Override host '%s'[%d]\n", m_Name.c_str(), m_Type);
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::OverrideDevice()
    {
        if (!m_DataPtr)
            AllocateOnHost();
        if (!m_DeviceDataPtr)
            AllocateOnDevice();
        m_DataLocation = Device;
        STORAGE_DEBUG_INFO("Override device '%s'[%d]\n", m_Name.c_str(), m_Type);
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::IncDeviceRef(size_t n)
    {
        NEURO_ASSERT(m_Type & ST_DeviceRefCounted, "Increasing ref count for non-refcounted storage.");
        m_DeviceDataRefCount += (int)n;
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::DecDeviceRef(size_t n)
    {
        NEURO_ASSERT(m_Type & ST_DeviceRefCounted, "Decreasing ref count for non-refcounted storage.");
        NEURO_ASSERT(n <= m_DeviceDataRefCount, "Over-decresing ref count.");
        m_DeviceDataRefCount -= (int)n;

        if (m_DeviceDataRefCount <= 0 && (m_Type & ST_DeviceRefCounted))
        {
            STORAGE_DEBUG_INFO("Ref count zeroed '%s'[%d] <<< deallocating device memory.\n", m_Name.c_str(), m_Type);
            FreeOnDevice();
        }
    }

    //////////////////////////////////////////////////////////////////////////
    float* Storage::Data()
    {
        if (!m_DataPtr)
            AllocateOnHost();
        return m_DataPtr;
    }

    //////////////////////////////////////////////////////////////////////////
    float* Storage::DeviceData()
    {
        NEURO_ASSERT(m_DeviceDataPtr, "Attempting to write to unallocated device memory.");
        NEURO_ASSERT(m_DataLocation == Device, "Attempting to write to data not located on device.");
        return m_DeviceDataPtr;
    }

    //////////////////////////////////////////////////////////////////////////
    void Storage::CopyWithinDevice(void* destDevPtr) const
    {
        NEURO_ASSERT(m_DeviceDataPtr, "Invalid device pointer.");
        NEURO_ASSERT(destDevPtr, "Invalid destination device pointer.");
        CUDA_CHECK(cudaMemcpy(destDevPtr, (void*)m_DeviceDataPtr, SizeInBytes(), cudaMemcpyDeviceToDevice));
    }
}
