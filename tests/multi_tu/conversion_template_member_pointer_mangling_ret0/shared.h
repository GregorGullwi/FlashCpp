#pragma once

struct ObjectOwner {
	int data;
};

struct FunctionOwner {
	int run();
};

using ObjectPtr = int ObjectOwner::*;
using FunctionPtr = int (FunctionOwner::*)();

int consume_object(ObjectPtr pointer);
int consume_function(FunctionPtr pointer);
int identity_owner_object(int ObjectOwner::* pointer);
int identity_owner_function(int (FunctionOwner::* pointer)());
