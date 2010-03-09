/** ou_thread.cpp
  * implements the Thread class
  * Author: Vijay Mathew Pandyalakal
  * Date: 13-OCT-2003
**/

/* Copyright 2000 - 2005 Vijay Mathew Pandyalakal.  All rights reserved.
 *
 * This software may be used or modified for any purpose, personal or
 * commercial.  Open Source redistributions are permitted.  
 *
 * Redistributions qualify as "Open Source" under  one of the following terms:
 *   
 *    Redistributions are made at no charge beyond the reasonable cost of
 *    materials and delivery.
 *
 *    Redistributions are accompanied by a copy of the Source Code or by an
 *    irrevocable offer to provide a copy of the Source Code for up to three
 *    years at the cost of materials and delivery.  Such redistributions
 *    must allow further use, modification, and redistribution of the Source
 *    Code under substantially the same terms as this license.
 *
 * Redistributions of source code must retain the copyright notices as they
 * appear in each source code file, these license terms, and the
 * disclaimer/limitation of liability set forth as paragraph 6 below.
 *
 * Redistributions in binary form must reproduce this Copyright Notice,
 * these license terms, and the disclaimer/limitation of liability set
 * forth as paragraph 6 below, in the documentation and/or other materials
 * provided with the distribution.
 *
 * The Software is provided on an "AS IS" basis.  No warranty is
 * provided that the Software is free of defects, or fit for a
 * particular purpose.  
 *
 * Limitation of Liability. The Author shall not be liable
 * for any damages suffered by the Licensee or any third party resulting
 * from use of the Software.
 */

#include <string>
using namespace std;

#include <windows.h>

#include "ou_thread.h"
using namespace openutils;

const int Thread::P_ABOVE_NORMAL = THREAD_PRIORITY_ABOVE_NORMAL;
const int Thread::P_BELOW_NORMAL = THREAD_PRIORITY_BELOW_NORMAL;
const int Thread::P_HIGHEST = THREAD_PRIORITY_HIGHEST;
const int Thread::P_IDLE = THREAD_PRIORITY_IDLE;
const int Thread::P_LOWEST = THREAD_PRIORITY_LOWEST;
const int Thread::P_NORMAL = THREAD_PRIORITY_NORMAL;
const int Thread::P_CRITICAL = THREAD_PRIORITY_TIME_CRITICAL;

/**@ The Thread class implementation
**@/

/** Thread()
  * default constructor
**/  
Thread::Thread() {
	m_hThread = NULL;
	m_strName = "null";
}

/** Thread(const char* nm)
  * overloaded constructor
  * creates a Thread object identified by "nm"
**/  
Thread::Thread(const char* nm) {
	m_hThread = NULL;
	m_strName = nm;
}

Thread::~Thread() {
	if(m_hThread != NULL) {
		stop();
	}
}

/** setName(const char* nm)
  * sets the Thread object's name to "nm"
**/  
void Thread::setName(const char* nm) {	
	m_strName = nm;
}

/** getName()
  * return the Thread object's name as a string
**/  
string Thread::getName() const {	
	return m_strName;
}

/** run()
  * called by the thread callback _ou_thread_proc()
  * to be overridden by child classes of Thread
**/ 
void Thread::run() {
	// Base run
}

/** sleep(long ms)
  * holds back the thread's execution for
  * "ms" milliseconds
**/ 
void Thread::sleep(long ms) {
	Sleep(ms);
}

/** start()
  * creates a low-level thread object and calls the
  * run() function
**/ 
void Thread::start() {
	DWORD tid = 0;	
	m_hThread = (unsigned long*)CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)_ou_thread_proc,(Thread*)this,0,&tid);
	if(m_hThread == NULL) {
		throw ThreadException("Failed to create thread");
	}else {
		setPriority(Thread::P_NORMAL);
	}
}

/** stop()
  * stops the running thread and frees the thread handle
**/ 
void Thread::stop() {
	if(m_hThread == NULL) return;	
	WaitForSingleObject(m_hThread,INFINITE);
	CloseHandle(m_hThread);
	m_hThread = NULL;
}

/** setPriority(int tp)
  * sets the priority of the thread to "tp"
  * "tp" must be a valid priority defined in the
  * Thread class
**/ 
void Thread::setPriority(int tp) {
	if(m_hThread == NULL) {
		throw ThreadException("Thread object is null");
	}else {
		if(SetThreadPriority(m_hThread,tp) == 0) {
			throw ThreadException("Failed to set priority");
		}
	}
}

/** suspend()  
  * suspends the thread
**/ 
void Thread::suspend() {
	if(m_hThread == NULL) {
		throw ThreadException("Thread object is null");
	}else {
		if(SuspendThread(m_hThread) < 0) {
			throw ThreadException("Failed to suspend thread");
		}
	}
}

/** resume()  
  * resumes a suspended thread
**/ 
void Thread::resume() {
	if(m_hThread == NULL) {
		throw ThreadException("Thread object is null");
	}else {
		if(ResumeThread(m_hThread) < 0) {
			throw ThreadException("Failed to resume thread");
		}
	}
}

/** wait(const char* m,long ms)  
  * makes the thread suspend execution until the
  * mutex represented by "m" is released by another thread.
  * "ms" specifies a time-out for the wait operation.
  * "ms" defaults to 5000 milli-seconds
**/ 
bool Thread::wait(const char* m,long ms) {
	HANDLE h = OpenMutex(MUTEX_ALL_ACCESS,FALSE,m);
	if(h == NULL) {
		throw ThreadException("Mutex not found");
	}
	DWORD d = WaitForSingleObject(h,ms);
	switch(d) {
	case WAIT_ABANDONED:
		throw ThreadException("Mutex not signaled");
		break;
	case WAIT_OBJECT_0:
		return true;
	case WAIT_TIMEOUT:
		throw ThreadException("Wait timed out");
		break;
	}
	return false;
}

/** release(const char* m)  
  * releases the mutex "m" and makes it 
  * available for other threads
**/ 
void Thread::release(const char* m) {
	HANDLE h = OpenMutex(MUTEX_ALL_ACCESS,FALSE,m);
	if(h == NULL) {
		throw ThreadException("Invalid mutex handle");
	}
	if(ReleaseMutex(h) == 0) {
		throw ThreadException("Failed to release mutex");
	}
}

/**@ The Mutex class implementation
**@/

/** Mutex()
  * default constructor
**/  
Mutex::Mutex() {
	m_hMutex = NULL;
	m_strName = "";
}

/** Mutex(const char* nm)
  * overloaded constructor
  * creates a Mutex object identified by "nm"
**/  
Mutex::Mutex(const char* nm) {	
	m_strName = nm;	
	m_hMutex = (unsigned long*)CreateMutex(NULL,FALSE,nm);
	if(m_hMutex == NULL) {
		throw ThreadException("Failed to create mutex");
	}
}

/** create(const char* nm)
  * frees the current mutex handle.
  * creates a Mutex object identified by "nm"
**/  
void Mutex::create(const char* nm) {
	if(m_hMutex != NULL) {
		CloseHandle(m_hMutex);
		m_hMutex = NULL;
	}
	m_strName = nm;
	m_hMutex = (unsigned long*)CreateMutex(NULL,FALSE,nm);
	if(m_hMutex == NULL) {
		throw ThreadException("Failed to create mutex");
	}
}
/** getMutexHandle()
  * returns the handle of the low-level mutex object
**/  
unsigned long* Mutex::getMutexHandle() {
	return m_hMutex;
}

/** getName()
  * returns the name of the mutex
**/ 
string Mutex::getName() {
	return m_strName;
}

void Mutex::release() {
	if(m_hMutex != NULL) {
		CloseHandle(m_hMutex);
	}
}

Mutex::~Mutex() {
	/*if(m_hMutex != NULL) {
		CloseHandle(m_hMutex);
	}*/
}

// ThreadException
ThreadException::ThreadException(const char* m) {
	msg = m;
}

string ThreadException::getMessage() const {
	return msg;
}

// global thread caallback
unsigned int _ou_thread_proc(void* param) {
	Thread* tp = (Thread*)param;
	tp->run();
	return 0;
}