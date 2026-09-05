#include <CoreAudio/CoreAudio.h>
#include <stdio.h>
#include <pthread.h>
static FILE* lg(void){ static FILE* f; if(!f){ f=fopen("/tmp/motu-trace2.txt","w"); } return f; }
static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER;
static int seq=0;
#define FC(x) (char)((x)>>24),(char)((x)>>16),(char)((x)>>8),(char)(x)

OSStatus my_get(AudioObjectID o,const AudioObjectPropertyAddress*a,UInt32 qs,const void*q,UInt32*ds,void*out){
  OSStatus r=AudioObjectGetPropertyData(o,a,qs,q,ds,out);
  pthread_mutex_lock(&mu); FILE*f=lg();
  fprintf(f,"%3d GET  obj=%-4u %c%c%c%c/%c%c%c%c/%u size=%-5u rc=%d",++seq,o,FC(a->mSelector),FC(a->mScope),a->mElement,ds?*ds:0,(int)r);
  if(a->mSelector=='Mapi'&&out) fprintf(f,"   >>> Mapi ptr=%p",*(void**)out);
  fprintf(f,"\n"); fflush(f); pthread_mutex_unlock(&mu);
  return r;
}
OSStatus my_has(AudioObjectID o,const AudioObjectPropertyAddress*a){
  Boolean r=AudioObjectHasProperty(o,a);
  pthread_mutex_lock(&mu); FILE*f=lg();
  fprintf(f,"%3d HAS  obj=%-4u %c%c%c%c/%c%c%c%c/%u -> %d\n",++seq,o,FC(a->mSelector),FC(a->mScope),a->mElement,(int)r);
  fflush(f); pthread_mutex_unlock(&mu); return r;
}
OSStatus my_addlis(AudioObjectID o,const AudioObjectPropertyAddress*a,AudioObjectPropertyListenerProc p,void*c){
  OSStatus r=AudioObjectAddPropertyListener(o,a,p,c);
  pthread_mutex_lock(&mu); FILE*f=lg();
  fprintf(f,"%3d LIS  obj=%-4u %c%c%c%c/%c%c%c%c/%u rc=%d\n",++seq,o,FC(a->mSelector),FC(a->mScope),a->mElement,(int)r);
  fflush(f); pthread_mutex_unlock(&mu); return r;
}
#define IP(n,r,e) __attribute__((used)) static struct{const void*a;const void*b;} n \
  __attribute__((section("__DATA,__interpose")))={(const void*)(unsigned long)&r,(const void*)(unsigned long)&e};
IP(_i1,my_get,AudioObjectGetPropertyData)
IP(_i2,my_has,AudioObjectHasProperty)
IP(_i3,my_addlis,AudioObjectAddPropertyListener)
