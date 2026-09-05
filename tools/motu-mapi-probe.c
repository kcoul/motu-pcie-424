#include <CoreAudio/CoreAudio.h>
#include <stdio.h>
#include <string.h>
static AudioDeviceID findDev(const char *want){
  AudioObjectPropertyAddress a={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementWildcard};
  UInt32 sz=0; if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,NULL,&sz)) return 0;
  int n=sz/sizeof(AudioDeviceID); AudioDeviceID ids[n];
  if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&sz,ids)) return 0;
  for(int i=0;i<n;i++){
    AudioObjectPropertyAddress na={kAudioDevicePropertyDeviceNameCFString,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementWildcard};
    CFStringRef nm=NULL; UInt32 ns=sizeof(nm);
    if(AudioObjectGetPropertyData(ids[i],&na,0,NULL,&ns,&nm)==noErr&&nm){
      char buf[256]; CFStringGetCString(nm,buf,256,kCFStringEncodingUTF8); CFRelease(nm);
      printf("  device %u: %s\n",ids[i],buf);
      if(strstr(buf,want)) return ids[i];
    }
  }
  return 0;
}
int main(void){
  printf("Enumerating CoreAudio devices:\n");
  AudioDeviceID d=findDev("PCI-424");
  if(!d){printf("PCI-424 NOT FOUND\n");return 1;}
  printf("\nPCI-424 deviceID=%u\n",d);
  AudioObjectPropertyAddress m={'Mapi',kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
  printf("'Mapi' hasProperty: %s\n",AudioObjectHasProperty(d,&m)?"YES":"NO");
  unsigned char buf[8]; memset(buf,0,8); *(UInt32*)buf=0x0E;
  UInt32 sz=8; OSStatus st=AudioObjectGetPropertyData(d,&m,0,NULL,&sz,buf);
  void *p=*(void**)buf;
  printf("'Mapi' get status=%d (%s)  returned ptr=%p\n",(int)st,st==noErr?"noErr":"ERROR",p);
  const UInt32 sel[]={'Mvol','csc#','csrc','rnlp'};
  for(int i=0;i<4;i++){AudioObjectPropertyAddress a={sel[i],kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    printf("  '%c%c%c%c' present: %s\n",(char)(sel[i]>>24),(char)(sel[i]>>16),(char)(sel[i]>>8),(char)sel[i],AudioObjectHasProperty(d,&a)?"YES":"NO");}
  return 0;
}
