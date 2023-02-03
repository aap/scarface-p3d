#pragma once

#include "core.h"

template <class T> class ListLink
{
public:
	ListLink<T> *next;
	ListLink<T> *prev;

	void Init(void) {
		next = nil;
		prev = nil;
	}
};

template <class T> class LinkedList
{
public:
	ListLink<T> anchor;
	core::i32 length;

	LinkedList(void) { Init(); }
	void Init(void) {
		anchor.Init();
		length = 0;
	}
	void Insert(ListLink<T> *link) {
		link->next = anchor.next;
		if(anchor.next)
			anchor.next->prev = link;
		else
			anchor.prev = link;
		anchor.next = link;
		link->prev = nil;
		length++;
	}
	void Remove(ListLink<T> *link) {
		if(link->prev)
			link->prev->next = link->next;
		else
			anchor.next = link->next;
		if(link->next)
			link->next->prev = link->prev;
		else
			anchor.prev = link->prev;
		length--;
	}
};
