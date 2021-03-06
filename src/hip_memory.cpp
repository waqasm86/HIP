/*
Copyright (c) 2015-2016 Advanced Micro Devices, Inc. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANNTY OF ANY KIND, EXPRESS OR
IMPLIED, INNCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANNY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER INN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR INN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "hip_runtime.h"
#include "hcc_detail/hip_hcc.h"
#include "hcc_detail/trace_helper.h"
#include <hsa.h>
#include <hc_am.hpp>
#include <hsa_ext_amd.h>
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// Memory
//
//
//

//---
/**
 * @return #hipSuccess, #hipErrorInvalidValue, #hipErrorInvalidDevice
 */
hipError_t hipPointerGetAttributes(hipPointerAttribute_t *attributes, void* ptr)
{
    HIP_INIT_API(attributes, ptr);

    hipError_t e = hipSuccess;

    hc::accelerator acc;
    hc::AmPointerInfo amPointerInfo(NULL, NULL, 0, acc, 0, 0);
    am_status_t status = hc::am_memtracker_getinfo(&amPointerInfo, ptr);
    if (status == AM_SUCCESS) {

        attributes->memoryType    = amPointerInfo._isInDeviceMem ? hipMemoryTypeDevice: hipMemoryTypeHost;
        attributes->hostPointer   = amPointerInfo._hostPointer;
        attributes->devicePointer = amPointerInfo._devicePointer;
        attributes->isManaged     = 0;
        if(attributes->memoryType == hipMemoryTypeHost){
            attributes->hostPointer = ptr;
        }
        if(attributes->memoryType == hipMemoryTypeDevice){
            attributes->devicePointer = ptr;
        }
        attributes->allocationFlags = amPointerInfo._appAllocationFlags;
        attributes->device          = amPointerInfo._appId;

        if (attributes->device < 0) {
            e = hipErrorInvalidDevice;
        }


    } else {
        attributes->memoryType    = hipMemoryTypeDevice;
        attributes->hostPointer   = 0;
        attributes->devicePointer = 0;
        attributes->device        = -1;
        attributes->isManaged     = 0;
        attributes->allocationFlags = 0;

        e = hipErrorUnknown; // TODO - should be hipErrorInvalidValue ?
    }

    return ihipLogStatus(e);
}


/**
 * @returns #hipSuccess,
 * @returns #hipErrorInvalidValue if flags are not 0
 * @returns #hipErrorMemoryAllocation if hostPointer is not a tracked allocation.
 */
hipError_t hipHostGetDevicePointer(void **devicePointer, void *hostPointer, unsigned flags)
{
    HIP_INIT_API(devicePointer, hostPointer, flags);

    hipError_t e = hipSuccess;

    *devicePointer = NULL;

    // Flags must be 0:
    if (flags != 0) {
        e = hipErrorInvalidValue;
    } else {
        hc::accelerator acc;
        hc::AmPointerInfo amPointerInfo(NULL, NULL, 0, acc, 0, 0);
        am_status_t status = hc::am_memtracker_getinfo(&amPointerInfo, hostPointer);
        if (status == AM_SUCCESS) {
            *devicePointer = amPointerInfo._devicePointer;
        } else {
            e = hipErrorMemoryAllocation;
        }
    }
    return ihipLogStatus(e);
}




//---
/**
 * @returns #hipSuccess #hipErrorMemoryAllocation
 */
hipError_t hipMalloc(void** ptr, size_t sizeBytes)
{
    HIP_INIT_API(ptr, sizeBytes);

    hipError_t  hip_status = hipSuccess;

	auto device = ihipGetTlsDefaultDevice();

    if (device) {
        const unsigned am_flags = 0;
        *ptr = hc::am_alloc(sizeBytes, device->_acc, am_flags);

        if (sizeBytes && (*ptr == NULL)) {
            hip_status = hipErrorMemoryAllocation;
        } else {
            hc::am_memtracker_update(*ptr, device->_device_index, 0);
            {
                LockedAccessor_DeviceCrit_t crit(device->criticalData());
                if (crit->peerCnt()) {
                    hsa_amd_agents_allow_access(crit->peerCnt(), crit->peerAgents(), NULL, *ptr);
                }
            }
        }
    } else {
        hip_status = hipErrorMemoryAllocation;
    }

    return ihipLogStatus(hip_status);
}



hipError_t hipHostMalloc(void** ptr, size_t sizeBytes, unsigned int flags)
{
    HIP_INIT_API(ptr, sizeBytes, flags);

    hipError_t hip_status = hipSuccess;

    auto device = ihipGetTlsDefaultDevice();

    if(device){
        if(flags == hipHostMallocDefault){
            *ptr = hc::am_alloc(sizeBytes, device->_acc, amHostPinned);
            if(sizeBytes < 1 && (*ptr == NULL)){
                hip_status = hipErrorMemoryAllocation;
            }else{
                hc::am_memtracker_update(*ptr, device->_device_index, amHostPinned);
            }
            tprintf(DB_MEM, " %s: pinned ptr=%p\n", __func__, *ptr);
        } else if(flags & hipHostMallocMapped){
            *ptr = hc::am_alloc(sizeBytes, device->_acc, amHostPinned);
            if(sizeBytes && (*ptr == NULL)){
                hip_status = hipErrorMemoryAllocation;
            }else{
                hc::am_memtracker_update(*ptr, device->_device_index, flags);
                {
                    LockedAccessor_DeviceCrit_t crit(device->criticalData());
                    if (crit->peerCnt()) {
                        hsa_amd_agents_allow_access(crit->peerCnt(), crit->peerAgents(), NULL, *ptr);
                    }
                }
            }
            tprintf(DB_MEM, " %s: pinned ptr=%p\n", __func__, *ptr);
        }
    }
    return ihipLogStatus(hip_status);
}


//---
// TODO - remove me, this is deprecated.
hipError_t hipHostAlloc(void** ptr, size_t sizeBytes, unsigned int flags)
{
    return hipHostMalloc(ptr, sizeBytes, flags);
};


//---
// TODO - remove me, this is deprecated.
hipError_t hipMallocHost(void** ptr, size_t sizeBytes)
{
    return hipHostMalloc(ptr, sizeBytes, 0);
}

// width in bytes
hipError_t hipMallocPitch(void** ptr, size_t* pitch, size_t width, size_t height) {

  HIP_INIT_API(ptr, pitch, width, height);

  hipError_t  hip_status = hipSuccess;

  if(width == 0 || height == 0)
    return ihipLogStatus(hipErrorUnknown);

  // hardcoded 128 bytes
  *pitch = ((((int)width-1)/128) + 1)*128;
  const size_t sizeBytes = (*pitch)*height;

  auto device = ihipGetTlsDefaultDevice();

  //err = hipMalloc(ptr, (*pitch)*height);
  if (device) {
    const unsigned am_flags = 0;
    *ptr = hc::am_alloc(sizeBytes, device->_acc, am_flags);

    if (sizeBytes && (*ptr == NULL)) {
      hip_status = hipErrorMemoryAllocation;
    } else {
      hc::am_memtracker_update(*ptr, device->_device_index, 0);
      {
        LockedAccessor_DeviceCrit_t crit(device->criticalData());
        if (crit->peerCnt() > 1) { // peerCnt includes self so only call allow_access if other peers involved:
          hsa_status_t hsa_status = hsa_amd_agents_allow_access(crit->peerCnt(), crit->peerAgents(), NULL, *ptr);
          if (hsa_status != HSA_STATUS_SUCCESS) {
            hip_status = hipErrorMemoryAllocation;
          }
        }
      }
    }
  } else {
      hip_status = hipErrorMemoryAllocation;
  }

  return ihipLogStatus(hip_status);

}

hipChannelFormatDesc hipCreateChannelDesc(int x, int y, int z, int w, hipChannelFormatKind f) {
  hipChannelFormatDesc cd;
  cd.x = x; cd.y = y; cd.z = z; cd.w = w;
  cd.f = f;
  return cd;
}

hipError_t hipMallocArray(hipArray** array, const hipChannelFormatDesc* desc,
                          size_t width, size_t height, unsigned int flags) {

    HIP_INIT_API(array, desc, width, height, flags);

    hipError_t  hip_status = hipSuccess;

    auto device = ihipGetTlsDefaultDevice();

    *array = (hipArray*)malloc(sizeof(hipArray));
    array[0]->width = width;
    array[0]->height = height;

    array[0]->f = desc->f;

    void ** ptr = &array[0]->data;

    if (device) {
      const unsigned am_flags = 0;
      const size_t size = width*height;

      switch(desc->f) {
        case hipChannelFormatKindSigned:
          *ptr = hc::am_alloc(size*sizeof(int), device->_acc, am_flags);
          break;
        case hipChannelFormatKindUnsigned:
          *ptr = hc::am_alloc(size*sizeof(unsigned int), device->_acc, am_flags);
          break;
        case hipChannelFormatKindFloat:
          *ptr = hc::am_alloc(size*sizeof(float), device->_acc, am_flags);
          break;
        case hipChannelFormatKindNone:
          *ptr = hc::am_alloc(size*sizeof(size_t), device->_acc, am_flags);
          break;
        default:
          hip_status = hipErrorUnknown;
          break;
      }
      if (size && (*ptr == NULL)) {
          hip_status = hipErrorMemoryAllocation;
      } else {
          hc::am_memtracker_update(*ptr, device->_device_index, 0);
          {
              LockedAccessor_DeviceCrit_t crit(device->criticalData());
              if (crit->peerCnt() > 1) { // peerCnt includes self so only call allow_access if other peers involved:
                  hsa_status_t hsa_status = hsa_amd_agents_allow_access(crit->peerCnt(), crit->peerAgents(), NULL, *ptr);
                  if (hsa_status != HSA_STATUS_SUCCESS) {
                      hip_status = hipErrorMemoryAllocation; 
                  }
              }
          }
      }

    } else {
        hip_status = hipErrorMemoryAllocation;
    }

    return ihipLogStatus(hip_status);
}


//---
hipError_t hipHostGetFlags(unsigned int* flagsPtr, void* hostPtr)
{
    HIP_INIT_API(flagsPtr, hostPtr);

	hipError_t hip_status = hipSuccess;

	hc::accelerator acc;
	hc::AmPointerInfo amPointerInfo(NULL, NULL, 0, acc, 0, 0);
	am_status_t status = hc::am_memtracker_getinfo(&amPointerInfo, hostPtr);
	if(status == AM_SUCCESS){
		*flagsPtr = amPointerInfo._appAllocationFlags;
		if(*flagsPtr == 0){
			hip_status = hipErrorInvalidValue;
		}
		else{
			hip_status = hipSuccess;
		}
		tprintf(DB_MEM, " %s: host ptr=%p\n", __func__, hostPtr);
	}else{
		hip_status = hipErrorInvalidValue;
	}
	return ihipLogStatus(hip_status);
}


//---
hipError_t hipHostRegister(void *hostPtr, size_t sizeBytes, unsigned int flags)
{
    HIP_INIT_API(hostPtr, sizeBytes, flags);

    hipError_t hip_status = hipSuccess;

    auto device = ihipGetTlsDefaultDevice();
    if(hostPtr == NULL){
        return ihipLogStatus(hipErrorInvalidValue);
    }

    hc::accelerator acc;
    hc::AmPointerInfo amPointerInfo(NULL, NULL, 0, acc, 0, 0);
    am_status_t am_status = hc::am_memtracker_getinfo(&amPointerInfo, hostPtr);

    if(am_status == AM_SUCCESS){
        hip_status = hipErrorHostMemoryAlreadyRegistered;
    }else{
        auto device = ihipGetTlsDefaultDevice();
        if(hostPtr == NULL){
            return ihipLogStatus(hipErrorInvalidValue);
        }
        if(device){
            if(flags == hipHostRegisterDefault || flags == hipHostRegisterPortable || flags == hipHostRegisterMapped){
                std::vector<hc::accelerator>vecAcc;
                for(int i=0;i<g_deviceCnt;i++){
                    vecAcc.push_back(g_devices[i]._acc);
                }
                am_status = hc::am_memory_host_lock(device->_acc, hostPtr, sizeBytes, &vecAcc[0], vecAcc.size());
                if(am_status == AM_SUCCESS){
                    hip_status = hipSuccess;
                }else{
                    hip_status = hipErrorMemoryAllocation;
                }
            }else{
                hip_status = hipErrorInvalidValue;
            }
        }
    }
    return ihipLogStatus(hip_status);
}

//---
hipError_t hipHostUnregister(void *hostPtr)
{
    HIP_INIT_API(hostPtr);
    auto device = ihipGetTlsDefaultDevice();
    hipError_t hip_status = hipSuccess;
    if(hostPtr == NULL){
        hip_status = hipErrorInvalidValue;
    }else{
        am_status_t am_status = hc::am_memory_host_unlock(device->_acc, hostPtr);
        if(am_status != AM_SUCCESS){
            hip_status = hipErrorHostMemoryNotRegistered;
        }
    }
    return ihipLogStatus(hip_status);
}


//---
hipError_t hipMemcpyToSymbol(const char* symbolName, const void *src, size_t count, size_t offset, hipMemcpyKind kind)
{
    HIP_INIT_API(symbolName, src, count, offset, kind);

#ifdef USE_MEMCPYTOSYMBOL
	if(kind != hipMemcpyHostToDevice)
	{
		return ihipLogStatus(hipErrorInvalidValue);
	}
	auto device = ihipGetTlsDefaultDevice();

    //hsa_signal_t depSignal;
    //int depSignalCnt = device._default_stream->preCopyCommand(NULL, &depSignal, ihipCommandCopyH2D);
    assert(0);  // Need to properly synchronize the copy - do something with depSignal if != NULL.

	device->_acc.memcpy_symbol(symbolName, (void*) src,count, offset);
#endif
    return ihipLogStatus(hipSuccess);
}

//---
hipError_t hipMemcpy(void* dst, const void* src, size_t sizeBytes, hipMemcpyKind kind)
{
    HIP_INIT_API(dst, src, sizeBytes, kind);

    hipStream_t stream = ihipSyncAndResolveStream(hipStreamNull);

    hc::completion_future marker;

    hipError_t e = hipSuccess;

    try {

        stream->locked_copySync(dst, src, sizeBytes, kind);
    }
    catch (ihipException ex) {
        e = ex._code;
    }



    return ihipLogStatus(e);
}


/**
 * @result #hipSuccess, #hipErrorInvalidDevice, #hipErrorInvalidMemcpyDirection, 
 * @result #hipErrorInvalidValue : If dst==NULL or src==NULL, or other bad argument.
 * @warning on HCC hipMemcpyAsync does not support overlapped H2D and D2H copies.
 * @warning on HCC hipMemcpyAsync requires that any host pointers are pinned (ie via the hipMallocHost call).
 */
//---
hipError_t hipMemcpyAsync(void* dst, const void* src, size_t sizeBytes, hipMemcpyKind kind, hipStream_t stream)
{
    HIP_INIT_API(dst, src, sizeBytes, kind, stream);

    hipError_t e = hipSuccess;

    stream = ihipSyncAndResolveStream(stream);


    if ((dst == NULL) || (src == NULL)) {
        e= hipErrorInvalidValue;
    } else if (stream) {
        try {
            stream->copyAsync(dst, src, sizeBytes, kind);
        }
        catch (ihipException ex) {
            e = ex._code;
        }
    } else {
        e = hipErrorInvalidValue;
    }

    return ihipLogStatus(e);
}

// dpitch, spitch, and width in bytes
hipError_t hipMemcpy2D(void* dst, size_t dpitch, const void* src, size_t spitch,
                       size_t width, size_t height, hipMemcpyKind kind) {

  HIP_INIT_API(dst, dpitch, src, spitch, width, height, kind);

  if(width > dpitch || width > spitch)
    return ihipLogStatus(hipErrorUnknown);

  hipStream_t stream = ihipSyncAndResolveStream(hipStreamNull);

  hc::completion_future marker;

  hipError_t e = hipSuccess;

  try {
    for(int i = 0; i < height; ++i) {
      stream->locked_copySync((unsigned char*)dst + i*dpitch, (unsigned char*)src + i*spitch, width, kind);
    }
  }
  catch (ihipException ex) {
    e = ex._code;
  }

  return ihipLogStatus(e);
}

// wOffset, width, and spitch in bytes
hipError_t hipMemcpy2DToArray(hipArray* dst, size_t wOffset, size_t hOffset, const void* src,
                                    size_t spitch, size_t width, size_t height, hipMemcpyKind kind) {

  HIP_INIT_API(dst, wOffset, hOffset, src, spitch, width, height, kind);

  hipStream_t stream = ihipSyncAndResolveStream(hipStreamNull);

  hc::completion_future marker;

  hipError_t e = hipSuccess;

  size_t byteSize;
  if(dst) {
    switch(dst[0].f) {
      case hipChannelFormatKindSigned:
        byteSize = sizeof(int);
        break;
      case hipChannelFormatKindUnsigned:
        byteSize = sizeof(unsigned int);
        break;
      case hipChannelFormatKindFloat:
        byteSize = sizeof(float);
        break;
      case hipChannelFormatKindNone:
        byteSize = sizeof(size_t);
        break;
      default:
        byteSize = 0;
        break;
    }
  } else {
    return ihipLogStatus(hipErrorUnknown);
  }

  if((wOffset + width > (dst->width * byteSize)) || width > spitch) {
    return ihipLogStatus(hipErrorUnknown);
  }

  size_t src_w = spitch;
  size_t dst_w = (dst->width)*byteSize;

  try {
    for(int i = 0; i < height; ++i) {
      stream->locked_copySync((unsigned char*)dst->data + i*dst_w, (unsigned char*)src + i*src_w, width, kind);
    }
  }
  catch (ihipException ex) {
    e = ex._code;
  }

  return ihipLogStatus(e);
}

hipError_t hipMemcpyToArray(hipArray* dst, size_t wOffset, size_t hOffset,
                            const void* src, size_t count, hipMemcpyKind kind) {

  HIP_INIT_API(dst, wOffset, hOffset, src, count, kind);

  hipStream_t stream = ihipSyncAndResolveStream(hipStreamNull);

  hc::completion_future marker;

  hipError_t e = hipSuccess;

  try {
    stream->locked_copySync((char *)dst->data + wOffset, src, count, kind);
  }
  catch (ihipException ex) {
    e = ex._code;
  }

  return ihipLogStatus(e);
}


// TODO-sync: function is async unless target is pinned host memory - then these are fully sync.
/** @return #hipErrorInvalidValue
 */
hipError_t hipMemsetAsync(void* dst, int  value, size_t sizeBytes, hipStream_t stream )
{
    HIP_INIT_API(dst, value, sizeBytes, stream);

    hipError_t e = hipSuccess;

    stream =  ihipSyncAndResolveStream(stream);

    if (stream) {
        stream->lockopen_preKernelCommand();

        hc::completion_future cf ;

        if ((sizeBytes & 0x3) == 0) {
            // use a faster dword-per-workitem copy:
            try {
                value = value & 0xff;
                unsigned value32 = (value << 24) | (value << 16) | (value << 8) | (value) ;
                cf = ihipMemsetKernel<unsigned> (stream, static_cast<unsigned*> (dst), value32, sizeBytes/sizeof(unsigned));
            }
            catch (std::exception &ex) {
                e = hipErrorInvalidValue;
            }
        } else {
            // use a slow byte-per-workitem copy:
            try {
                cf = ihipMemsetKernel<char> (stream, static_cast<char*> (dst), value, sizeBytes);
            }
            catch (std::exception &ex) {
                e = hipErrorInvalidValue;
            }
        }

        stream->lockclose_postKernelCommand(cf);


        if (HIP_LAUNCH_BLOCKING) {
            tprintf (DB_SYNC, "'%s' LAUNCH_BLOCKING wait for memset [stream:%p].\n", __func__, (void*)stream);
            cf.wait();
            tprintf (DB_SYNC, "'%s' LAUNCH_BLOCKING memset completed [stream:%p].\n", __func__, (void*)stream);
        }
    } else {
        e = hipErrorInvalidValue;
    }


    return ihipLogStatus(e);
};


hipError_t hipMemset(void* dst, int  value, size_t sizeBytes )
{
    hipStream_t stream = hipStreamNull;
    // TODO - call an ihip memset so HIP_TRACE is correct.
    HIP_INIT_API(dst, value, sizeBytes, stream);

    hipError_t e = hipSuccess;

    stream =  ihipSyncAndResolveStream(stream);

    if (stream) {
        stream->lockopen_preKernelCommand();

        hc::completion_future cf ;

        if ((sizeBytes & 0x3) == 0) {
            // use a faster dword-per-workitem copy:
            try {
                value = value & 0xff;
                unsigned value32 = (value << 24) | (value << 16) | (value << 8) | (value) ;
                cf = ihipMemsetKernel<unsigned> (stream, static_cast<unsigned*> (dst), value32, sizeBytes/sizeof(unsigned));
            }
            catch (std::exception &ex) {
                e = hipErrorInvalidValue;
            }
        } else {
            // use a slow byte-per-workitem copy:
            try {
                cf = ihipMemsetKernel<char> (stream, static_cast<char*> (dst), value, sizeBytes);
            }
            catch (std::exception &ex) {
                e = hipErrorInvalidValue;
            }
        }
        cf.wait();

        stream->lockclose_postKernelCommand(cf);


        if (HIP_LAUNCH_BLOCKING) {
            tprintf (DB_SYNC, "'%s' LAUNCH_BLOCKING wait for memset [stream:%p].\n", __func__, (void*)stream);
            cf.wait();
            tprintf (DB_SYNC, "'%s' LAUNCH_BLOCKING memset completed [stream:%p].\n", __func__, (void*)stream);
        }
    } else {
        e = hipErrorInvalidValue;
    }

    return ihipLogStatus(e);
}


/*
 * @returns #hipSuccess, #hipErrorInvalidDevice, #hipErrorInvalidValue (if free != NULL due to bug)S
 * @warning On HCC, the free memory only accounts for memory allocated by this process and may be optimistic.
 */
hipError_t hipMemGetInfo  (size_t *free, size_t *total)
{
    HIP_INIT_API(free, total);

    hipError_t e = hipSuccess;

    ihipDevice_t * hipDevice = ihipGetTlsDefaultDevice();
    if (hipDevice) {
        if (total) {
            *total = hipDevice->_props.totalGlobalMem;
        }

        if (free) {
            // TODO - replace with kernel-level for reporting free memory:
            size_t deviceMemSize, hostMemSize, userMemSize;
            hc::am_memtracker_sizeinfo(hipDevice->_acc, &deviceMemSize, &hostMemSize, &userMemSize);
            printf ("deviceMemSize=%zu\n", deviceMemSize);
        
            *free =  hipDevice->_props.totalGlobalMem - deviceMemSize;
        }

    } else {
        e = hipErrorInvalidDevice;
    }

    return ihipLogStatus(e);
}


//---
hipError_t hipFree(void* ptr)
{
    HIP_INIT_API(ptr);

    hipError_t hipStatus = hipErrorInvalidDevicePointer;

   // Synchronize to ensure all work has finished.
    ihipGetTlsDefaultDevice()->locked_waitAllStreams(); // ignores non-blocking streams, this waits for all activity to finish.

    if (ptr) {
        hc::accelerator acc;
        hc::AmPointerInfo amPointerInfo(NULL, NULL, 0, acc, 0, 0);
        am_status_t status = hc::am_memtracker_getinfo(&amPointerInfo, ptr);
        if(status == AM_SUCCESS){
            if(amPointerInfo._hostPointer == NULL){
                hc::am_free(ptr);
                hipStatus = hipSuccess;
            }
        }
    } else {
        // free NULL pointer succeeds and is common technique to initialize runtime
        hipStatus = hipSuccess;  
    }

    return ihipLogStatus(hipStatus);
}


hipError_t hipHostFree(void* ptr)
{
    HIP_INIT_API(ptr);

   // Synchronize to ensure all work has finished.
    ihipGetTlsDefaultDevice()->locked_waitAllStreams(); // ignores non-blocking streams, this waits for all activity to finish.


    hipError_t hipStatus = hipErrorInvalidValue;
    if (ptr) {
        hc::accelerator acc;
        hc::AmPointerInfo amPointerInfo(NULL, NULL, 0, acc, 0, 0);
        am_status_t status = hc::am_memtracker_getinfo(&amPointerInfo, ptr);
        if(status == AM_SUCCESS){
            if(amPointerInfo._hostPointer == ptr){
                hc::am_free(ptr);
                hipStatus = hipSuccess;
            }
        }
    } else {
        // free NULL pointer succeeds and is common technique to initialize runtime
        hipStatus = hipSuccess;  
    }

    return ihipLogStatus(hipStatus);
};


// TODO - deprecated function.
hipError_t hipFreeHost(void* ptr)
{
    return hipHostFree(ptr);
}

hipError_t hipFreeArray(hipArray* array)
{
  HIP_INIT_API(array);

  hipError_t hipStatus = hipErrorInvalidDevicePointer;

  // Synchronize to ensure all work has finished.
  ihipGetTlsDefaultDevice()->locked_waitAllStreams(); // ignores non-blocking streams, this waits for all activity to finish.

  if(array->data) {
    hc::accelerator acc;
    hc::AmPointerInfo amPointerInfo(NULL, NULL, 0, acc, 0, 0);
    am_status_t status = hc::am_memtracker_getinfo(&amPointerInfo, array->data);
    if(status == AM_SUCCESS){
      if(amPointerInfo._hostPointer == NULL){
        hc::am_free(array->data);
          hipStatus = hipSuccess;
      }
    }
  }

  return ihipLogStatus(hipStatus);
}


