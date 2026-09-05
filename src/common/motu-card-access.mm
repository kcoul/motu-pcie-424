#import <Cocoa/Cocoa.h>
#import <CoreAudio/CoreAudio.h>
#include <stdio.h>
#include <string.h>

static FILE *L;
#define P(...) do{ fprintf(L,__VA_ARGS__); fflush(L);}while(0)

typedef void*(*Fn_ret_ptr)(void*,void*);
typedef int  (*Fn_ret_int)(void*,void*);
typedef int  (*Fn_int_i)(void*,void*,int);
typedef void (*Fn_name)(void*,void*,int,char*,int);
typedef int  (*Fn_3ip)(void*,void*,int*,int*,int*);
typedef int  (*Fn_2ip)(void*,void*,int*,int*);

static OSStatus noopListener(AudioObjectID o,UInt32 n,const AudioObjectPropertyAddress*a,void*c){return noErr;}
static void* slot(void* obj, int n){ void** vt=*(void***)obj; return vt[n]; }

static AudioDeviceID findDev(){
  AudioObjectPropertyAddress a={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementWildcard};
  UInt32 sz=0; AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,NULL,&sz);
  int n=sz/sizeof(AudioDeviceID); AudioDeviceID ids[n];
  AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&sz,ids);
  for(int i=0;i<n;i++){AudioObjectPropertyAddress na={kAudioDevicePropertyDeviceNameCFString,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementWildcard};
    CFStringRef nm=NULL;UInt32 ns=sizeof(nm);
    if(AudioObjectGetPropertyData(ids[i],&na,0,NULL,&ns,&nm)==noErr&&nm){char b[256];CFStringGetCString(nm,b,256,kCFStringEncodingUTF8);CFRelease(nm);
      if(strstr(b,"PCI-424")) return ids[i];}}
  return 0;
}
// Tell the CoreAudio HAL to run its notifications on THIS run loop.
// Without this the HAL uses its own internal thread, and the MOTU plugin
// refuses to hand out the AudioWireCard pointer (it compares its stored
// run loop against CFRunLoopGetCurrent()).
static void initHalRunLoop(){
  CFRunLoopRef rl = CFRunLoopGetCurrent();
  AudioObjectPropertyAddress a = { 'rnlp', kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
  OSStatus st = AudioObjectSetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, (UInt32)sizeof(CFRunLoopRef), &rl);
  P("initHalRunLoop: set 'rnlp' -> %p  status=%d\n",(void*)rl,(int)st);
}
static void run(){
  L=fopen("/tmp/motu-spike.txt","w");
  initHalRunLoop();
  AudioDeviceID d=findDev();
  P("device=%u  mainRunLoop=%p current=%p\n",d,(void*)CFRunLoopGetMain(),(void*)CFRunLoopGetCurrent());
  // replicate the working app: install property listeners first
  AudioObjectPropertyAddress l1={'nsrt',kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
  AudioObjectAddPropertyListener(d,&l1,noopListener,NULL);
  AudioObjectPropertyAddress l2={'dch2',kAudioObjectPropertyScopeOutput,kAudioObjectPropertyElementMain};
  AudioObjectAddPropertyListener(d,&l2,noopListener,NULL);
  AudioObjectPropertyAddress l3={'dch2',kAudioObjectPropertyScopeInput,kAudioObjectPropertyElementMain};
  AudioObjectAddPropertyListener(d,&l3,noopListener,NULL);
  AudioObjectPropertyAddress l4={'csrc',kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
  AudioObjectAddPropertyListener(d,&l4,noopListener,NULL);
  P("4 listeners installed (incl csrc)\n");
  CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.5,false);
  AudioObjectPropertyAddress m={'Mapi',kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
  unsigned char buf[8]; memset(buf,0,8); *(UInt32*)buf=0x0E; UInt32 sz=8;
  void* card=NULL; OSStatus st=noErr;
  for(int attempt=0; attempt<40 && !card; attempt++){
    AudioDeviceID dd = findDev();          // re-enumerate, as the working app does
    memset(buf,0,8); *(UInt32*)buf=0x0E; sz=8;
    st=AudioObjectGetPropertyData(dd,&m,0,NULL,&sz,buf);
    card=*(void**)buf;
    if(!card){ CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.25,false); }
    else P("Mapi OK on attempt %d (%.2fs)\n",attempt,attempt*0.25);
  }
  P("Mapi status=%d card=%p\n",(int)st,card);
  if(!card){ P("NO CARD OBJECT - stopping\n"); fclose(L); return; }
  unsigned char exc[144]; memset(exc,0,144);

  P("\n-- AudioWireCard --\n");
  P("CardGestalt(14)      = %d\n", ((Fn_int_i)slot(card,4))(card,exc,14));
  int wires = ((Fn_ret_int)slot(card,11))(card,exc);
  P("GetNumWires          = %d\n", wires);
  for(int i=0;i<wires;i++){
    memset(exc,0,144);
    int conn = ((Fn_int_i)slot(card,12))(card,exc,i);
    char nm[128]; memset(nm,0,128);
    if(conn) ((Fn_name)slot(card,14))(card,exc,i,nm,127);
    P("  wire %d connected=%d name=\"%s\"\n", i, conn, nm);
  }
  memset(exc,0,144);
  P("GetNumInputs         = %d\n", ((Fn_ret_int)slot(card,16))(card,exc));
  P("GetNumActiveInputs   = %d\n", ((Fn_ret_int)slot(card,17))(card,exc));
  P("GetNumOutputs        = %d\n", ((Fn_ret_int)slot(card,22))(card,exc));
  P("GetNumActiveOutputs  = %d\n", ((Fn_ret_int)slot(card,23))(card,exc));
  P("GetSMUXOptionCapable = %d\n", ((Fn_ret_int)slot(card,34))(card,exc));
  P("GetSMUXOptionSetting = %d\n", ((Fn_ret_int)slot(card,32))(card,exc));

  P("\n-- CueMixAPI --\n");
  memset(exc,0,144);
  void* cue = ((Fn_ret_ptr)slot(card,5))(card,exc);
  P("GetCueMixAPI         = %p\n", cue);
  if(cue){
    int a=-1,b=-1,c=-1; memset(exc,0,144);
    ((Fn_3ip)slot(cue,22))(cue,exc,&a,&b,&c);
    P("GetCueMixResourceUsage = %d, %d, %d\n",a,b,c);
    int p1=-1,p2=-1; memset(exc,0,144);
    ((Fn_2ip)slot(cue,23))(cue,exc,&p1,&p2);
    P("GetPCIUsage            = %d, %d\n",p1,p2);
  }
  memset(exc,0,144);
  void* smpte = ((Fn_ret_ptr)slot(card,6))(card,exc);
  P("GetSMPTEAPI          = %p\n", smpte);
  memset(exc,0,144);
  void* talk = ((Fn_ret_ptr)slot(card,7))(card,exc);
  P("GetTalkbackAPI       = %p\n", talk);
  P("\nDONE\n");
  fclose(L);
}
@interface D:NSObject<NSApplicationDelegate> @end
@implementation D
-(void)applicationDidFinishLaunching:(NSNotification*)n{
  // No AVFoundation. First CoreAudio touch happens here, on the main thread,
  // so the plugin captures the MAIN run loop.
  dispatch_async(dispatch_get_main_queue(), ^{ run(); [NSApp terminate:nil]; });
}
@end
int main(){ @autoreleasepool{ NSApplication*a=[NSApplication sharedApplication];
  [a setActivationPolicy:NSApplicationActivationPolicyRegular];
  D*d=[D new]; [a setDelegate:d]; [a run]; } return 0; }
