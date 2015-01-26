
#include <node.h>
#include <v8.h>

#include <node_buffer.h>
#include <node_object_wrap.h>

#ifndef NOFREEIMAGE
#include <FreeImage.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <math.h>


using namespace node;
using namespace v8;



#define MAXDIFF 0x4000 //16384

#ifndef NOFREEIMAGE
struct Baton {
	uv_work_t request;
	Persistent<Function> callback;

	FIMEMORY* fiMemoryOut;
	FIMEMORY* fiMemoryIn;
	FIBITMAP* bitmap;
	u_int8_t* bits;
	u_int16_t sum;
};
#endif

typedef struct {
	u_int8_t b2:4;
	u_int8_t b1:4;
} bit4;

typedef union
{
	u_int32_t u32;
	struct {
		u_int8_t b1;
		u_int8_t b2;
		u_int8_t b3;
		u_int8_t b4;
	} u8;

	struct {
		bit4 b1;
		bit4 b2;
		bit4 b3;
		bit4 b4;
	} b4;

} cmp32;

uint cmp4bit(const void *data1,const void *data2){

		uint retdiff = 0;

		cmp32 * u32data1 = (cmp32 *)data1;
		cmp32 * u32data2 = (cmp32 *)data2;

		for(int i=0;i < 128 ;i++){


			//printf("%d: %X = %x \n",i,u32data1->u32,u32data2->u32);

			if(u32data1->u32 != u32data2->u32){
				if(u32data1->u8.b1 != u32data2->u8.b1){
					retdiff += abs(u32data1->b4.b1.b1 - u32data2->b4.b1.b1);
					retdiff += abs(u32data1->b4.b1.b2 - u32data2->b4.b1.b2);
				}
				if(u32data1->u8.b2 != u32data2->u8.b2){
					retdiff += abs(u32data1->b4.b2.b1 - u32data2->b4.b2.b1);
					retdiff += abs(u32data1->b4.b2.b2 - u32data2->b4.b2.b2);
				}
				if(u32data1->u8.b3 != u32data2->u8.b3){
					retdiff += abs(u32data1->b4.b3.b1 - u32data2->b4.b3.b1);
					retdiff += abs(u32data1->b4.b3.b2 - u32data2->b4.b3.b2);
				}
				if(u32data1->u8.b4 != u32data2->u8.b4){
					retdiff += abs(u32data1->b4.b4.b1 - u32data2->b4.b4.b1);
					retdiff += abs(u32data1->b4.b4.b2 - u32data2->b4.b4.b2);
				}
			}
			u32data1++;
			u32data2++;
		}

		return retdiff;
}

struct BatonCompare {
	uv_work_t request;
	Persistent<Function> callback;

	uint8_t *data1;
	uint8_t *data2;
	float val;
};

static void compareWork(uv_work_t* req) {
	BatonCompare* baton = static_cast<BatonCompare*>(req->data);
	baton->val = (float)cmp4bit(baton->data1,baton->data2) / MAXDIFF;
}



static void compareAfter(uv_work_t* req) {
		HandleScope scope;
		BatonCompare* baton = static_cast<BatonCompare*>(req->data);

		const unsigned argc = 2;
		Local<Value> argv[argc] = {
			Local<Value>::New( Null() ),
			Local<Value>::New( Number::New(baton->val)),
		};

		TryCatch try_catch;
		baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
		if (try_catch.HasCaught())
			FatalException(try_catch);

		baton->callback.Dispose();

		delete baton;
}


static Handle<Value> compare(const Arguments& args) {
	HandleScope scope;
	if (args.Length() < 2)
		return ThrowException( Exception::TypeError(String::New("Expecting 2 arguments")));

	if (!Buffer::HasInstance(args[0]))
		return ThrowException( Exception::TypeError( String::New("argument  1 must be a Buffer")));

	if (!Buffer::HasInstance(args[1]))
		return ThrowException( Exception::TypeError( String::New("argument 2 must be a Buffer")));



#if NODE_MAJOR_VERSION == 0 && NODE_MINOR_VERSION < 10
	Local < Object > buffer1_obj = args[0]->ToObject();
	Local < Object > buffer2_obj = args[1]->ToObject();
#else
	Local<Value> buffer1_obj = args[0];
	Local<Value> buffer2_obj = args[1];
#endif

	if (Buffer::Length(buffer1_obj) != 512) {
		return ThrowException(Exception::TypeError(String::New("Buffer::Length(buffer1_obj) != 512")));
	}

	if (Buffer::Length(buffer2_obj) != 512) {
		return ThrowException(Exception::TypeError(String::New("Buffer::Length(buffer2_obj) != 512")));
	}

	if (args.Length() > 2 && args[2]->IsFunction()){
		Local < Function > callback = Local < Function > ::Cast(args[2]);

		BatonCompare* baton = new BatonCompare();
		baton->request.data = baton;
		baton->callback = Persistent < Function > ::New(callback);
		baton->data1 = (uint8_t *)Buffer::Data(buffer1_obj);
		baton->data2 = (uint8_t *)Buffer::Data(buffer2_obj);
		baton->val = 1;


		int status = uv_queue_work(uv_default_loop(), &baton->request, compareWork, (uv_after_work_cb) compareAfter);

		assert(status == 0);
		return Undefined();
	}else{
		float val = (float)cmp4bit((const void*)Buffer::Data(buffer1_obj),(const void*)Buffer::Data(buffer2_obj)) / MAXDIFF;

		return scope.Close(Number::New(val));
	}

}

#ifndef NOFREEIMAGE

static void imageVectorWork(uv_work_t* req) {

	Baton* baton = static_cast<Baton*>(req->data);
	//uint i;
	//ImageThumb* obj = baton->obj;

	FIMEMORY * fiMemoryIn = NULL;
	FIMEMORY * fiMemoryOut = NULL;
	FIBITMAP * fiBitmap = NULL, *thumbnail1 = NULL, *thumbnail2 = NULL;
	uint8_t * bits = NULL;

	fiMemoryIn = baton->fiMemoryIn;	//FreeImage_OpenMemory((BYTE *)baton->imageBuffer,baton->imageBufferLength);

	FREE_IMAGE_FORMAT format = FreeImage_GetFileTypeFromMemory(fiMemoryIn);

	if (format < 0)
		goto ret;

	fiBitmap = FreeImage_LoadFromMemory(format, fiMemoryIn);

	if (!fiBitmap)
		goto ret;

//	FILTER_BOX		  = 0,	// Box, pulse, Fourier window, 1st order (constant) b-spline
//	FILTER_BICUBIC	  = 1,	// Mitchell & Netravali's two-param cubic filter
//	FILTER_BILINEAR   = 2,	// Bilinear filter
//	FILTER_BSPLINE	*  = 3,	// 4th order (cubic) b-spline
//	FILTER_CATMULLROM = 4,	// Catmull-Rom spline, Overhauser spline
//	FILTER_LANCZOS3	  = 5	// Lanczos3 filter

	thumbnail1 = FreeImage_Rescale(fiBitmap, 32, 32, FILTER_BSPLINE);

	if (!thumbnail1)
		goto ret;

	thumbnail2 = FreeImage_ConvertTo4Bits(thumbnail1);

	if (!thumbnail2)
		goto ret;


	bits = (uint8_t *) FreeImage_GetBits(thumbnail2);
	baton->bits = bits;


	baton->bitmap = thumbnail2;

	for(int i =0; i < 512 ;i++){
		bit4 *bit4s1 = (bit4 *)(baton->bits + i);
		baton->sum += bit4s1->b1 + bit4s1->b2;
	}

#if 0

	for(int y =0; y < 32 ;y++){
		uint8_t *bitss =  baton->bits + (y*16);
		for (int x = 0; x < 16; x++) {
			bit4 *bit4s1 = (bit4 *)(bitss + x);

			uint8_t diff1 = abs(bit4s1->b1 - bit4s1->b2);
			bit4s1->b1 = diff1;
			if(x < 15 ){
				bit4 *bit4s2 = (bit4 *)(bitss + x + 1);
				uint8_t diff2 = abs(bit4s1->b2 - bit4s2->b1);
				bit4s1->b2 = diff2;
			}else{
				bit4s1->b2 = 0;
			}

		}
	}

#endif



	fiMemoryOut = FreeImage_OpenMemory();

	FreeImage_SaveToMemory( FIF_BMP, thumbnail2, fiMemoryOut, 0 );

	ret:

	if (fiMemoryIn)
		FreeImage_CloseMemory(fiMemoryIn);
	if (fiBitmap)
		FreeImage_Unload(fiBitmap);
	if (thumbnail1)
		FreeImage_Unload(thumbnail1);

	//FreeImage_Unload( thumbnail2 );

	baton->fiMemoryOut =  fiMemoryOut;

}

static void imageVectorAfter(uv_work_t* req) {
	HandleScope scope;
	Baton* baton = static_cast<Baton*>(req->data);

	if (baton->bits) {
		const unsigned argc = 4;
		const char*data;
		int datalen;
		FreeImage_AcquireMemory(baton->fiMemoryOut,(BYTE**)&data, (DWORD*)&datalen );
		Local<Value> argv[argc] = {
			Local<Value>::New( Null() ),
			Local<Object>::New( Buffer::New((const char*)baton->bits,512)->handle_) ,
			Local<Value>::New( Number::New(baton->sum)),
			Local<Object>::New( Buffer::New((const char*)data,datalen)->handle_)
		};

		TryCatch try_catch;
		baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
		if (try_catch.HasCaught())
			FatalException(try_catch);
	} else {
		Local < Value > err = Exception::Error(String::New("error"));

		const unsigned argc = 1;
		Local<Value> argv[argc] = {err};

		TryCatch try_catch;
		baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
		if (try_catch.HasCaught())
			FatalException(try_catch);
	}

	if (baton->fiMemoryOut)
		FreeImage_CloseMemory(baton->fiMemoryOut);

	if (baton->bitmap)
		FreeImage_Unload(baton->bitmap);

	baton->callback.Dispose();
	delete baton;
}

static Handle<Value> imageVector(const Arguments& args) {
	HandleScope scope;


	if (args.Length() < 2)
		return ThrowException(Exception::TypeError(String::New("Expecting 2 arguments")));

	if (!Buffer::HasInstance(args[0]))
		return ThrowException( Exception::TypeError( String::New("First argument must be a Buffer")));

	if (!args[1]->IsFunction())
		return ThrowException( Exception::TypeError( String::New( "Second argument must be a function")));


	Local < Function > callback = Local < Function > ::Cast(args[1]);

#if NODE_MAJOR_VERSION == 0 && NODE_MINOR_VERSION < 10
	Local < Object > buffer_obj = args[0]->ToObject();
#else
	Local<Value> buffer_obj = args[0];
#endif

	Baton* baton = new Baton();
	baton->request.data = baton;
	baton->callback = Persistent < Function > ::New(callback);


	baton->fiMemoryIn = FreeImage_OpenMemory((BYTE *) Buffer::Data(buffer_obj),
			Buffer::Length(buffer_obj));
	baton->fiMemoryOut = NULL;
	baton->bitmap = NULL;
	baton->bits = NULL;
	baton->sum = 0;

	int status = uv_queue_work(uv_default_loop(), &baton->request,
			imageVectorWork, (uv_after_work_cb) imageVectorAfter);

	assert(status == 0);
	return Undefined();
}

#endif


extern "C" {
	void init(Handle<Object> target) {
		HandleScope scope;
#ifndef NOFREEIMAGE
		target->Set(String::NewSymbol("imageVector"), FunctionTemplate::New(imageVector)->GetFunction());
#endif
		target->Set(String::NewSymbol("compare"), FunctionTemplate::New(compare)->GetFunction());
		target->Set(String::NewSymbol("maxDiff"), Number::New(MAXDIFF));
	}

	NODE_MODULE(imgcmp, init);
}
