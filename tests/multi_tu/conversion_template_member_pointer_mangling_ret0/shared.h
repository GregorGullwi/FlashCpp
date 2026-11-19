#pragma once

struct ObjectOwner {
	int data;
};

struct FunctionOwner {
	int run();
};

template<class T>
struct SpecializedOwner {
	T value;
	int get();
};

using ObjectPtr = int ObjectOwner::*;
using FunctionPtr = int (FunctionOwner::*)();
using SpecializedIntPtr = int SpecializedOwner<int>::*;
using SpecializedLongPtr = int SpecializedOwner<long>::*;
using SpecializedIntFunctionPtr = int (SpecializedOwner<int>::*)();
using SpecializedLongFunctionPtr = int (SpecializedOwner<long>::*)();

int consume_object(ObjectPtr pointer);
int consume_function(FunctionPtr pointer);
int identity_owner_object(int ObjectOwner::* pointer);
int identity_owner_function(int (FunctionOwner::* pointer)());
int consume_specialized_int(SpecializedIntPtr pointer);
int consume_specialized_long(SpecializedLongPtr pointer);
int consume_specialized_fn_int(SpecializedIntFunctionPtr pointer);
int consume_specialized_fn_long(SpecializedLongFunctionPtr pointer);
