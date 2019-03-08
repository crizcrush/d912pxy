/*
MIT License

Copyright(c) 2018-2019 megai2

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
#include "stdafx.h"
#include "d912pxy_vstream.h"

#define API_OVERHEAD_TRACK_LOCAL_ID_DEFINE PXY_METRICS_API_OVERHEAD_VSTREAM
#define D912PXY_METHOD_IMPL_CN d912pxy_vstream

D912PXY_IUNK_IMPL

/*** IDirect3DResource9 methods ***/
D912PXY_METHOD_IMPL(GetDevice)(THIS_ IDirect3DDevice9** ppDevice) { return GetDevice(ppDevice); }
D912PXY_METHOD_IMPL(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) { return SetPrivateData(refguid, pData, SizeOfData, Flags); }
D912PXY_METHOD_IMPL(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) { return GetPrivateData(refguid, pData, pSizeOfData); }
D912PXY_METHOD_IMPL(FreePrivateData)(THIS_ REFGUID refguid) { return FreePrivateData(refguid); }
D912PXY_METHOD_IMPL_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) { return SetPriority(PriorityNew); }
D912PXY_METHOD_IMPL_(DWORD, GetPriority)(THIS) { return GetPriority(); }
D912PXY_METHOD_IMPL_(void, PreLoad)(THIS) { return PreLoad(); }
D912PXY_METHOD_IMPL_(D3DRESOURCETYPE, GetType)(THIS) { return GetType(); }

D912PXY_METHOD_IMPL(GetDesc)(THIS_ D3DVERTEXBUFFER_DESC *pDesc)
{
	return D3DERR_INVALIDCALL;
}

d912pxy_vstream::d912pxy_vstream(d912pxy_device * dev, UINT Length, DWORD Usage, DWORD fmt, DWORD isIB) : d912pxy_resource(dev, isIB ? RTID_IBUF : RTID_VBUF, isIB ? L"vstream i" : L"vstream v")
{
	d12res_buffer(Length, D3D12_HEAP_TYPE_DEFAULT);

	lockDepth = 0;

	data = malloc(Length);

	dx9desc.FVF = 0;
	dx9desc.Pool = D3DPOOL_DEFAULT;
	dx9desc.Format = D3DFMT_UNKNOWN;
	if (isIB)
		dx9desc.Type = D3DRTYPE_INDEXBUFFER;
	else 
		dx9desc.Type = D3DRTYPE_VERTEXBUFFER;
	dx9desc.Size = Length;	
	dx9desc.Usage = Usage;

	NoteFormatChange(fmt, isIB);

	uploadResSel = 0;

	opFlags = 0;
}

d912pxy_vstream::~d912pxy_vstream()
{
	if (data)
		free(data);
}

D912PXY_METHOD_IMPL(Lock)(THIS_ UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags)
{
	API_OVERHEAD_TRACK_START(2)

	if (!SizeToLock)
	{
		lockOffsets[lockDepth] = 0;
		lockSizes[lockDepth] = dx9desc.Size - OffsetToLock;
	}
	else {
		lockOffsets[lockDepth] = OffsetToLock;
		lockSizes[lockDepth] = SizeToLock;
	}
	++lockDepth;

	if (lockDepth >= PXY_INNER_MAX_LOCK_DEPTH)
	{
		LOG_ERR_THROW2(-1, "lockDepth >= PXY_INNER_MAX_LOCK_DEPTH");
	}
	
	*ppbData = (void*)((intptr_t)(data) + OffsetToLock);

	API_OVERHEAD_TRACK_END(2)

	return D3D_OK;
}

D912PXY_METHOD_IMPL(Unlock)(THIS)
{
	API_OVERHEAD_TRACK_START(2)

	opFlags |= PXY_INNER_BUFFER_FLAG_DIRTY;
	--lockDepth;
	d912pxy_s(bufloadThread)->IssueUpload(this, uploadRes[uploadResSel], lockOffsets[lockDepth], lockSizes[lockDepth]);

	API_OVERHEAD_TRACK_END(2)

	return D3D_OK;
}

void d912pxy_vstream::IFrameBindVB(UINT stride, UINT slot, UINT offset, ID3D12GraphicsCommandList * cl)
{
	bindData.v.StrideInBytes = stride;

	if (offset != 0)
	{
		D3D12_VERTEX_BUFFER_VIEW bindDataOffst;

		bindDataOffst = bindData.v;
		bindDataOffst.SizeInBytes -= offset;
		bindDataOffst.BufferLocation += offset;
		cl->IASetVertexBuffers(slot, 1, &bindDataOffst);
	}
	else
		cl->IASetVertexBuffers(slot, 1, &bindData.v);
}

void d912pxy_vstream::IFrameBindIB(ID3D12GraphicsCommandList * cl)
{
	cl->IASetIndexBuffer(&bindData.i);
}

void d912pxy_vstream::NoteFormatChange(DWORD fmt, DWORD isIB)
{
	if (isIB)
	{
		bindData.i.BufferLocation = m_res->GetGPUVirtualAddress();
		if (fmt == D3DFMT_INDEX16)
			bindData.i.Format = DXGI_FORMAT_R16_UINT;
		else
			bindData.i.Format = DXGI_FORMAT_R32_UINT;

		bindData.i.SizeInBytes = dx9desc.Size;
	}
	else {
		bindData.v.BufferLocation = m_res->GetGPUVirtualAddress();
		bindData.v.SizeInBytes = dx9desc.Size;
		bindData.v.StrideInBytes = 0;
	}
}

UINT d912pxy_vstream::FinalReleaseCB()
{
	if (d912pxy_s(pool_vstream))
	{
		EvictFromGPU();

		d912pxy_vstream* tv = this;
		d912pxy_s(pool_vstream)->PoolRW(d912pxy_s(pool_vstream)->MemCatFromSize(dx9desc.Size), &tv, 1);
		return 0;
	}
	else {
		return 1;
	}
}

IDirect3DVertexBuffer9 * d912pxy_vstream::AsDX9VB()
{
	return this;
}

IDirect3DIndexBuffer9 * d912pxy_vstream::AsDX9IB()
{
	return d912pxy_vstream_to_index(this);
}

UINT32 d912pxy_vstream::PooledAction(UINT32 use)
{
	d912pxy_s(pool_vstream)->PooledActionLock();

	if (!d912pxy_comhandler::PooledAction(use))
	{		
		d912pxy_s(pool_vstream)->PooledActionUnLock();
		return 0;
	}

	if (use)
	{		
		d12res_buffer(dx9desc.Size, D3D12_HEAP_TYPE_DEFAULT);
		data = malloc(dx9desc.Size);
	}
	else {
		m_res = nullptr;

		free(data);
		data = NULL;

		if (uploadRes[0] != 0)
		{
			uploadRes[0]->Release();
			uploadRes[0] = 0;
		}

		if (uploadRes[1] != 0)
		{
			uploadRes[1]->Release();
			uploadRes[1] = 0;
		}

	}

	d912pxy_s(pool_vstream)->PooledActionUnLock();

	return 0;
}

void d912pxy_vstream::AsyncUploadDataCopy(UINT32 offset, UINT32 size, ID3D12GraphicsCommandList * cl)
{
	if (!uploadRes[uploadResSel])
		CreateUploadBuffer(uploadResSel, dx9desc.Size);

	memcpy((void*)((intptr_t)mappedMem[uploadResSel] + offset), (void*)((intptr_t)data + offset), size);

	MakeGPUResident();

	IFrameBarrierTrans(0, D3D12_RESOURCE_STATE_COPY_DEST, CLG_BUF);

	AsyncBufferCopyPrepared(uploadRes[uploadResSel], offset, size, cl);

	IFrameBarrierTrans(0, D3D12_RESOURCE_STATE_GENERIC_READ, CLG_BUF);

	++swapRef;

	if (swapRef == 255)
		swapRef = 2;

	ThreadRef(-1);
	
}

#undef API_OVERHEAD_TRACK_LOCAL_ID_DEFINE
#undef D912PXY_METHOD_IMPL_CN