/*
 * Tests the panning and 3D functions of DirectSound
 *
 * Part of this test involves playing test tones. But this only makes
 * sense if someone is going to carefully listen to it, and would only
 * bother everyone else.
 * So this is only done if the test is being run in interactive mode.
 *
 * Copyright (c) 2002-2004 Francois Gouget
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define NONAMELESSSTRUCT
#define NONAMELESSUNION
#include <windows.h>

#include <math.h>
#include <stdlib.h>

#include "wine/test.h"
#include "windef.h"
#include "wingdi.h"
#include "dsound.h"
#include "dxerr8.h"

#include "dsound_test.h"

typedef struct {
    char* wave;
    DWORD wave_len;

    LPDIRECTSOUNDBUFFER dsbo;
    LPWAVEFORMATEX wfx;
    DWORD buffer_size;
    DWORD written;
    DWORD played;
    DWORD offset;
} play_state_t;

static int buffer_refill8(play_state_t* state, DWORD size)
{
    LPVOID ptr1,ptr2;
    DWORD len1,len2;
    HRESULT rc;

    if (size>state->wave_len-state->written)
        size=state->wave_len-state->written;

    rc=IDirectSoundBuffer_Lock(state->dsbo,state->offset,size,
                               &ptr1,&len1,&ptr2,&len2,0);
    ok(rc==DS_OK,"Lock: 0x%lx\n",rc);
    if (rc!=DS_OK)
        return -1;

    memcpy(ptr1,state->wave+state->written,len1);
    state->written+=len1;
    if (ptr2!=NULL) {
        memcpy(ptr2,state->wave+state->written,len2);
        state->written+=len2;
    }
    state->offset=state->written % state->buffer_size;
    rc=IDirectSoundBuffer_Unlock(state->dsbo,ptr1,len1,ptr2,len2);
    ok(rc==DS_OK,"Unlock: 0x%lx\n",rc);
    if (rc!=DS_OK)
        return -1;
    return size;
}

static int buffer_silence8(play_state_t* state, DWORD size)
{
    LPVOID ptr1,ptr2;
    DWORD len1,len2;
    HRESULT rc;
    BYTE s;

    rc=IDirectSoundBuffer_Lock(state->dsbo,state->offset,size,
                               &ptr1,&len1,&ptr2,&len2,0);
    ok(rc==DS_OK,"Lock: 0x%lx\n",rc);
    if (rc!=DS_OK)
        return -1;

    s=(state->wfx->wBitsPerSample==8?0x80:0);
    memset(ptr1,s,len1);
    if (ptr2!=NULL) {
        memset(ptr2,s,len2);
    }
    state->offset=(state->offset+size) % state->buffer_size;
    rc=IDirectSoundBuffer_Unlock(state->dsbo,ptr1,len1,ptr2,len2);
    ok(rc==DS_OK,"Unlock: 0x%lx\n",rc);
    if (rc!=DS_OK)
        return -1;
    return size;
}

static int buffer_service8(play_state_t* state)
{
    DWORD last_play_pos,play_pos,buf_free;
    HRESULT rc;

    rc=IDirectSoundBuffer_GetCurrentPosition(state->dsbo,&play_pos,NULL);
    ok(rc==DS_OK,"GetCurrentPosition: %lx\n",rc);
    if (rc!=DS_OK) {
        goto STOP;
    }

    /* Update the amount played */
    last_play_pos=state->played % state->buffer_size;
    if (play_pos<last_play_pos)
        state->played+=state->buffer_size-last_play_pos+play_pos;
    else
        state->played+=play_pos-last_play_pos;

    if (winetest_debug > 1)
        trace("buf size=%ld last_play_pos=%ld play_pos=%ld played=%ld / %ld\n",
              state->buffer_size,last_play_pos,play_pos,state->played,state->wave_len);

    if (state->played>state->wave_len)
    {
        /* Everything has been played */
        goto STOP;
    }

    /* Refill the buffer */
    if (state->offset<=play_pos)
        buf_free=play_pos-state->offset;
    else
        buf_free=state->buffer_size-state->offset+play_pos;

    if (winetest_debug > 1)
        trace("offset=%ld free=%ld written=%ld / %ld\n",
              state->offset,buf_free,state->written,state->wave_len);
    if (buf_free==0)
        return 1;

    if (state->written<state->wave_len)
    {
        int w=buffer_refill8(state,buf_free);
        if (w==-1)
            goto STOP;
        buf_free-=w;
        if (state->written==state->wave_len && winetest_debug > 1)
            trace("last sound byte at %ld\n",
                  (state->written % state->buffer_size));
    }

    if (buf_free>0) {
        /* Fill with silence */
        if (winetest_debug > 1)
            trace("writing %ld bytes of silence\n",buf_free);
        if (buffer_silence8(state,buf_free)==-1)
            goto STOP;
    }
    return 1;

STOP:
    if (winetest_debug > 1)
        trace("stopping playback\n");
    rc=IDirectSoundBuffer_Stop(state->dsbo);
    ok(rc==DS_OK,"Stop failed: rc=%ld\n",rc);
    return 0;
}

void test_buffer8(LPDIRECTSOUND8 dso, LPDIRECTSOUNDBUFFER dsbo,
                  BOOL is_primary, BOOL set_volume, LONG volume,
                  BOOL set_pan, LONG pan, BOOL play, double duration,
                  BOOL buffer3d, LPDIRECTSOUND3DLISTENER listener,
                  BOOL move_listener, BOOL move_sound)
{
    HRESULT rc;
    DSBCAPS dsbcaps;
    WAVEFORMATEX wfx,wfx2;
    DWORD size,status,freq;
    int ref;

    /* DSOUND: Error: Invalid caps pointer */
    rc=IDirectSoundBuffer_GetCaps(dsbo,0);
    ok(rc==DSERR_INVALIDPARAM,"GetCaps should have failed: 0x%lx\n",rc);

    ZeroMemory(&dsbcaps, sizeof(dsbcaps));

    /* DSOUND: Error: Invalid caps pointer */
    rc=IDirectSoundBuffer_GetCaps(dsbo,&dsbcaps);
    ok(rc==DSERR_INVALIDPARAM,"GetCaps should have failed: 0x%lx\n",rc);

    dsbcaps.dwSize=sizeof(dsbcaps);
    rc=IDirectSoundBuffer_GetCaps(dsbo,&dsbcaps);
    ok(rc==DS_OK,"GetCaps failed: 0x%lx\n",rc);
    if (rc==DS_OK) {
        trace("    Caps: flags=0x%08lx size=%ld\n",dsbcaps.dwFlags,
              dsbcaps.dwBufferBytes);
    }

    /* Query the format size. Note that it may not match sizeof(wfx) */
    size=0;
    rc=IDirectSoundBuffer_GetFormat(dsbo,NULL,0,&size);
    ok(rc==DS_OK && size!=0,
       "GetFormat should have returned the needed size: rc=0x%lx size=%ld\n",
       rc,size);

    rc=IDirectSoundBuffer_GetFormat(dsbo,&wfx,sizeof(wfx),NULL);
    ok(rc==DS_OK,"GetFormat failed: 0x%lx\n",rc);
    if (rc==DS_OK && is_primary) {
        trace("Primary buffer default format: tag=0x%04x %ldx%dx%d avg.B/s=%ld align=%d\n",
              wfx.wFormatTag,wfx.nSamplesPerSec,wfx.wBitsPerSample,
              wfx.nChannels,wfx.nAvgBytesPerSec,wfx.nBlockAlign);
    }

    /* DSOUND: Error: Invalid frequency buffer */
    rc=IDirectSoundBuffer_GetFrequency(dsbo,0);
    ok(rc==DSERR_INVALIDPARAM,"GetFrequency should have failed: 0x%lx\n",rc);

    /* DSOUND: Error: Primary buffers don't support CTRLFREQUENCY */
    rc=IDirectSoundBuffer_GetFrequency(dsbo,&freq);
    ok((rc==DS_OK && !is_primary) || (rc==DSERR_CONTROLUNAVAIL&&is_primary) ||
       (rc==DSERR_CONTROLUNAVAIL&&!(dsbcaps.dwFlags&DSBCAPS_CTRLFREQUENCY)),
       "GetFrequency failed: 0x%lx\n",rc);
    if (rc==DS_OK) {
        ok(freq==wfx.nSamplesPerSec,
           "The frequency returned by GetFrequency %ld does not match the format %ld\n",
           freq,wfx.nSamplesPerSec);
    }

    /* DSOUND: Error: Invalid status pointer */
    rc=IDirectSoundBuffer_GetStatus(dsbo,0);
    ok(rc==DSERR_INVALIDPARAM,"GetStatus should have failed: 0x%lx\n",rc);

    rc=IDirectSoundBuffer_GetStatus(dsbo,&status);
    ok(rc==DS_OK,"GetStatus failed: 0x%lx\n",rc);
    ok(status==0,"status=0x%lx instead of 0\n",status);

    if (is_primary) {
        /* We must call SetCooperativeLevel to be allowed to call SetFormat */
        /* DSOUND: Setting DirectSound cooperative level to DSSCL_PRIORITY */
        rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_PRIORITY);
        ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%0lx\n",rc);
        if (rc!=DS_OK)
            return;

        /* DSOUND: Error: Invalid format pointer */
        rc=IDirectSoundBuffer_SetFormat(dsbo,0);
        ok(rc==DSERR_INVALIDPARAM,"SetFormat should have failed: 0x%lx\n",rc);

        init_format(&wfx2,WAVE_FORMAT_PCM,11025,16,2);
        rc=IDirectSoundBuffer_SetFormat(dsbo,&wfx2);
        ok(rc==DS_OK,"SetFormat failed: 0x%lx\n",rc);

        /* There is no garantee that SetFormat will actually change the
	 * format to what we asked for. It depends on what the soundcard
	 * supports. So we must re-query the format.
	 */
        rc=IDirectSoundBuffer_GetFormat(dsbo,&wfx,sizeof(wfx),NULL);
        ok(rc==DS_OK,"GetFormat failed: 0x%lx\n",rc);
        if (rc==DS_OK &&
            (wfx.wFormatTag!=wfx2.wFormatTag ||
             wfx.nSamplesPerSec!=wfx2.nSamplesPerSec ||
             wfx.wBitsPerSample!=wfx2.wBitsPerSample ||
             wfx.nChannels!=wfx2.nChannels)) {
            trace("Requested format tag=0x%04x %ldx%dx%d avg.B/s=%ld align=%d\n",
                  wfx2.wFormatTag,wfx2.nSamplesPerSec,wfx2.wBitsPerSample,
                  wfx2.nChannels,wfx2.nAvgBytesPerSec,wfx2.nBlockAlign);
            trace("Got tag=0x%04x %ldx%dx%d avg.B/s=%ld align=%d\n",
                  wfx.wFormatTag,wfx.nSamplesPerSec,wfx.wBitsPerSample,
                  wfx.nChannels,wfx.nAvgBytesPerSec,wfx.nBlockAlign);
        }

        /* Set the CooperativeLevel back to normal */
        /* DSOUND: Setting DirectSound cooperative level to DSSCL_NORMAL */
        rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_NORMAL);
        ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%0lx\n",rc);
    }

    if (play) {
        play_state_t state;
        DS3DLISTENER listener_param;
        LPDIRECTSOUND3DBUFFER buffer=NULL;
        DS3DBUFFER buffer_param;
        DWORD start_time,now;

        trace("    Playing %g second 440Hz tone at %ldx%dx%d\n", duration,
              wfx.nSamplesPerSec, wfx.wBitsPerSample,wfx.nChannels);

        if (is_primary) {
            /* We must call SetCooperativeLevel to be allowed to call Lock */
            /* DSOUND: Setting DirectSound cooperative level to DSSCL_WRITEPRIMARY */
            rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_WRITEPRIMARY);
            ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%0lx\n",rc);
            if (rc!=DS_OK)
                return;
        }
        if (buffer3d) {
            LPDIRECTSOUNDBUFFER temp_buffer;

            rc=IDirectSoundBuffer_QueryInterface(dsbo,&IID_IDirectSound3DBuffer,(LPVOID *)&buffer);
            ok(rc==DS_OK,"QueryInterface failed: 0x%lx\n",rc);
            if (rc!=DS_OK)
                return;

            /* check the COM interface */
            rc=IDirectSoundBuffer_QueryInterface(dsbo, &IID_IDirectSoundBuffer,(LPVOID *)&temp_buffer);
            ok(rc==DS_OK && temp_buffer!=NULL,"QueryInterface failed: 0x%lx\n",rc);
            ok(temp_buffer==dsbo,"COM interface broken: 0x%08lx != 0x%08lx\n",(DWORD)temp_buffer,(DWORD)dsbo);
            ref=IDirectSoundBuffer_Release(temp_buffer);
            ok(ref==1,"IDirectSoundBuffer_Release has %d references, should have 1\n",ref);

            temp_buffer=NULL;
            rc=IDirectSound3DBuffer_QueryInterface(dsbo, &IID_IDirectSoundBuffer,(LPVOID *)&temp_buffer);
            ok(rc==DS_OK && temp_buffer!=NULL,"IDirectSound3DBuffer_QueryInterface failed: 0x%lx\n",rc);
            ok(temp_buffer==dsbo,"COM interface broken: 0x%08lx != 0x%08lx\n",(DWORD)temp_buffer,(DWORD)dsbo);
            ref=IDirectSoundBuffer_Release(temp_buffer);
            ok(ref==1,"IDirectSoundBuffer_Release has %d references, should have 1\n",ref);

#if 0
            /* FIXME: this works on windows */
            ref=IDirectSoundBuffer_Release(dsbo);
            ok(ref==0,"IDirectSoundBuffer_Release has %d references, should have 0\n",ref);

            rc=IDirectSound3DBuffer_QueryInterface(buffer, &IID_IDirectSoundBuffer,(LPVOID *)&dsbo);
            ok(rc==DS_OK && dsbo!=NULL,"IDirectSound3DBuffer_QueryInterface failed: 0x%lx\n",rc);
#endif

            /* DSOUND: Error: Invalid buffer */
            rc=IDirectSound3DBuffer_GetAllParameters(buffer,0);
            ok(rc==DSERR_INVALIDPARAM,"IDirectSound3DBuffer_GetAllParameters failed: 0x%lx\n",rc);

            ZeroMemory(&buffer_param, sizeof(buffer_param));

            /* DSOUND: Error: Invalid buffer */
            rc=IDirectSound3DBuffer_GetAllParameters(buffer,&buffer_param);
            ok(rc==DSERR_INVALIDPARAM,"IDirectSound3DBuffer_GetAllParameters failed: 0x%lx\n",rc);

            buffer_param.dwSize=sizeof(buffer_param);
            rc=IDirectSound3DBuffer_GetAllParameters(buffer,&buffer_param);
            ok(rc==DS_OK,"IDirectSound3DBuffer_GetAllParameters failed: 0x%lx\n",rc);
        }
        if (set_volume) {
            if (dsbcaps.dwFlags & DSBCAPS_CTRLVOLUME) {
                LONG val;
                rc=IDirectSoundBuffer_GetVolume(dsbo,&val);
                ok(rc==DS_OK,"GetVolume failed: 0x%lx\n",rc);

                rc=IDirectSoundBuffer_SetVolume(dsbo,volume);
                ok(rc==DS_OK,"SetVolume failed: 0x%lx\n",rc);
            } else {
                /* DSOUND: Error: Buffer does not have CTRLVOLUME */
                rc=IDirectSoundBuffer_GetVolume(dsbo,&volume);
                ok(rc==DSERR_CONTROLUNAVAIL,"GetVolume should have failed: 0x%lx\n",rc);
            }
        }

        if (set_pan) {
            if (dsbcaps.dwFlags & DSBCAPS_CTRLPAN) {
                LONG val;
                rc=IDirectSoundBuffer_GetPan(dsbo,&val);
                ok(rc==DS_OK,"GetPan failed: 0x%lx\n",rc);

                rc=IDirectSoundBuffer_SetPan(dsbo,pan);
                ok(rc==DS_OK,"SetPan failed: 0x%lx\n",rc);
            } else {
                /* DSOUND: Error: Buffer does not have CTRLPAN */
                rc=IDirectSoundBuffer_GetPan(dsbo,&pan);
                ok(rc==DSERR_CONTROLUNAVAIL,"GetPan should have failed: 0x%lx\n",rc);
            }
        }

        state.wave=wave_generate_la(&wfx,duration,&state.wave_len);

        state.dsbo=dsbo;
        state.wfx=&wfx;
        state.buffer_size=dsbcaps.dwBufferBytes;
        state.played=state.written=state.offset=0;
        buffer_refill8(&state,state.buffer_size);

        rc=IDirectSoundBuffer_Play(dsbo,0,0,DSBPLAY_LOOPING);
        ok(rc==DS_OK,"Play: 0x%lx\n",rc);

        rc=IDirectSoundBuffer_GetStatus(dsbo,&status);
        ok(rc==DS_OK,"GetStatus failed: 0x%lx\n",rc);
        ok(status==(DSBSTATUS_PLAYING|DSBSTATUS_LOOPING),
           "GetStatus: bad status: %lx\n",status);

        if (listener) {
            ZeroMemory(&listener_param,sizeof(listener_param));
            listener_param.dwSize=sizeof(listener_param);
            rc=IDirectSound3DListener_GetAllParameters(listener,&listener_param);
            ok(rc==DS_OK,"IDirectSound3dListener_GetAllParameters failed 0x%lx\n",rc);
            if (move_listener)
            {
                listener_param.vPosition.x = -5.0;
                listener_param.vVelocity.x = 10.0/duration;
            }
            rc=IDirectSound3DListener_SetAllParameters(listener,&listener_param,DS3D_IMMEDIATE);
            ok(rc==DS_OK,"IDirectSound3dListener_SetPosition failed 0x%lx\n",rc);
        }
        if (buffer3d) {
            if (move_sound)
            {
                buffer_param.vPosition.x = 100.0;
                buffer_param.vVelocity.x = -200.0/duration;
            }
            buffer_param.flMinDistance = 10;
            rc=IDirectSound3DBuffer_SetAllParameters(buffer,&buffer_param,DS3D_IMMEDIATE);
            ok(rc==DS_OK,"IDirectSound3dBuffer_SetPosition failed 0x%lx\n",rc);
        }

        start_time=GetTickCount();
        while (buffer_service8(&state)) {
            WaitForSingleObject(GetCurrentProcess(),TIME_SLICE);
            now=GetTickCount();
            if (listener && move_listener) {
                listener_param.vPosition.x = -5.0+10.0*(now-start_time)/1000/duration;
                if (winetest_debug>2)
                    trace("listener position=%g\n",listener_param.vPosition.x);
                rc=IDirectSound3DListener_SetPosition(listener,listener_param.vPosition.x,listener_param.vPosition.y,listener_param.vPosition.z,DS3D_IMMEDIATE);
                ok(rc==DS_OK,"IDirectSound3dListener_SetPosition failed 0x%lx\n",rc);
            }
            if (buffer3d && move_sound) {
                buffer_param.vPosition.x = 100-200.0*(now-start_time)/1000/duration;
                if (winetest_debug>2)
                    trace("sound position=%g\n",buffer_param.vPosition.x);
                rc=IDirectSound3DBuffer_SetPosition(buffer,buffer_param.vPosition.x,buffer_param.vPosition.y,buffer_param.vPosition.z,DS3D_IMMEDIATE);
                ok(rc==DS_OK,"IDirectSound3dBuffer_SetPosition failed 0x%lx\n",rc);
            }
        }
        /* Check the sound duration was within 10% of the expected value */
        now=GetTickCount();
        ok(fabs(1000*duration-now+start_time)<=100*duration,"The sound played for %ld ms instead of %g ms\n",now-start_time,1000*duration);

        free(state.wave);
        if (is_primary) {
            /* Set the CooperativeLevel back to normal */
            /* DSOUND: Setting DirectSound cooperative level to DSSCL_NORMAL */
            rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_NORMAL);
            ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%0lx\n",rc);
        }
        if (buffer3d) {
            ref=IDirectSound3DBuffer_Release(buffer);
            ok(ref==0,"IDirectSound3DBuffer_Release has %d references, should have 0\n",ref);
        }
    }
}

static HRESULT test_secondary8(LPGUID lpGuid, int play,
                              int has_3d, int has_3dbuffer,
                              int has_listener, int has_duplicate,
                              int move_listener, int move_sound)
{
    HRESULT rc;
    LPDIRECTSOUND8 dso=NULL;
    LPDIRECTSOUNDBUFFER primary=NULL,secondary=NULL;
    LPDIRECTSOUND3DLISTENER listener=NULL;
    DSBUFFERDESC bufdesc;
    WAVEFORMATEX wfx;
    int ref;

    /* Create the DirectSound object */
    rc=DirectSoundCreate8(lpGuid,&dso,NULL);
    ok(rc==DS_OK,"DirectSoundCreate8 failed: 0x%08lx\n",rc);
    if (rc!=DS_OK)
        return rc;

    /* We must call SetCooperativeLevel before creating primary buffer */
    /* DSOUND: Setting DirectSound cooperative level to DSSCL_PRIORITY */
    rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_PRIORITY);
    ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%0lx\n",rc);
    if (rc!=DS_OK)
        goto EXIT;

    ZeroMemory(&bufdesc, sizeof(bufdesc));
    bufdesc.dwSize=sizeof(bufdesc);
    bufdesc.dwFlags=DSBCAPS_PRIMARYBUFFER;
    if (has_3d)
        bufdesc.dwFlags|=DSBCAPS_CTRL3D;
    else
        bufdesc.dwFlags|=(DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN);
    rc=IDirectSound8_CreateSoundBuffer(dso,&bufdesc,&primary,NULL);
    ok(rc==DS_OK && primary!=NULL,"CreateSoundBuffer failed to create a %sprimary buffer 0x%lx\n",has_3d?"3D ":"", rc);

    if (rc==DS_OK && primary!=NULL) {
        if (has_listener) {
            rc=IDirectSoundBuffer_QueryInterface(primary,&IID_IDirectSound3DListener,(void **)&listener);
            ok(rc==DS_OK && listener!=NULL,"IDirectSoundBuffer_QueryInterface failed to get a 3D listener 0x%lx\n",rc);
            ref=IDirectSoundBuffer_Release(primary);
            ok(ref==0,"IDirectSoundBuffer_Release primary has %d references, should have 0\n",ref);
            if (rc==DS_OK && listener!=NULL) {
                DS3DLISTENER listener_param;
                ZeroMemory(&listener_param,sizeof(listener_param));
                /* DSOUND: Error: Invalid buffer */
                rc=IDirectSound3DListener_GetAllParameters(listener,0);
                ok(rc==DSERR_INVALIDPARAM,"IDirectSound3dListener_GetAllParameters failed 0x%lx\n",rc);

                /* DSOUND: Error: Invalid buffer */
                rc=IDirectSound3DListener_GetAllParameters(listener,&listener_param);
                ok(rc==DSERR_INVALIDPARAM,"IDirectSound3dListener_GetAllParameters failed 0x%lx\n",rc);

                listener_param.dwSize=sizeof(listener_param);
                rc=IDirectSound3DListener_GetAllParameters(listener,&listener_param);
                ok(rc==DS_OK,"IDirectSound3dListener_GetAllParameters failed 0x%lx\n",rc);
            }
            else
                goto EXIT;
        }

        init_format(&wfx,WAVE_FORMAT_PCM,22050,16,2);
        secondary=NULL;
        ZeroMemory(&bufdesc, sizeof(bufdesc));
        bufdesc.dwSize=sizeof(bufdesc);
        bufdesc.dwFlags=DSBCAPS_GETCURRENTPOSITION2;
        if (has_3d)
            bufdesc.dwFlags|=DSBCAPS_CTRL3D;
        else
            bufdesc.dwFlags|=(DSBCAPS_CTRLFREQUENCY|DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN);
        bufdesc.dwBufferBytes=wfx.nAvgBytesPerSec*BUFFER_LEN/1000;
        bufdesc.lpwfxFormat=&wfx;
        trace("  Testing a %s%ssecondary buffer %s%s%s%sat %ldx%dx%d\n",
              has_3dbuffer?"3D ":"",
              has_duplicate?"duplicated ":"",
              listener!=NULL||move_sound?"with ":"",
              move_listener?"moving ":"",
              listener!=NULL?"listener ":"",
              listener&&move_sound?"and moving sound ":move_sound?"moving sound ":"",
              wfx.nSamplesPerSec,wfx.wBitsPerSample,wfx.nChannels);
        rc=IDirectSound8_CreateSoundBuffer(dso,&bufdesc,&secondary,NULL);
        ok(rc==DS_OK && secondary!=NULL,"CreateSoundBuffer failed to create a 3D secondary buffer 0x%lx\n",rc);
        if (rc==DS_OK && secondary!=NULL) {
            if (!has_3d)
            {
                LONG refvol,refpan,vol,pan;

                /* Check the initial secondary buffer's volume and pan */
                rc=IDirectSoundBuffer_GetVolume(secondary,&vol);
                ok(rc==DS_OK,"GetVolume(secondary) failed: %s\n",DXGetErrorString8(rc));
                ok(vol==0,"wrong volume for a new secondary buffer: %ld\n",vol);
                rc=IDirectSoundBuffer_GetPan(secondary,&pan);
                ok(rc==DS_OK,"GetPan(secondary) failed: %s\n",DXGetErrorString8(rc));
                ok(pan==0,"wrong pan for a new secondary buffer: %ld\n",pan);

                /* Check that changing the secondary buffer's volume and pan
                 * does not impact the primary buffer's volume and pan
                 */
                rc=IDirectSoundBuffer_GetVolume(primary,&refvol);
                ok(rc==DS_OK,"GetVolume(primary) failed: %s\n",DXGetErrorString8(rc));
                rc=IDirectSoundBuffer_GetPan(primary,&refpan);
                ok(rc==DS_OK,"GetPan(primary) failed: %s\n",DXGetErrorString8(rc));

                rc=IDirectSoundBuffer_SetVolume(secondary,-1000);
                ok(rc==DS_OK,"SetVolume(secondary) failed: %s\n",DXGetErrorString8(rc));
                rc=IDirectSoundBuffer_GetVolume(secondary,&vol);
                ok(rc==DS_OK,"SetVolume(secondary) failed: %s\n",DXGetErrorString8(rc));
                ok(vol==-1000,"secondary: wrong volume %ld instead of -1000\n",vol);
                rc=IDirectSoundBuffer_SetPan(secondary,-1000);
                ok(rc==DS_OK,"SetPan(secondary) failed: %s\n",DXGetErrorString8(rc));
                rc=IDirectSoundBuffer_GetPan(secondary,&pan);
                ok(rc==DS_OK,"SetPan(secondary) failed: %s\n",DXGetErrorString8(rc));
                ok(vol==-1000,"secondary: wrong pan %ld instead of -1000\n",pan);

                rc=IDirectSoundBuffer_GetVolume(primary,&vol);
                ok(rc==DS_OK,"GetVolume(primary) failed: %s\n",DXGetErrorString8(rc));
                ok(vol==refvol,"The primary volume changed from %ld to %ld\n",refvol,vol);
                rc=IDirectSoundBuffer_GetPan(primary,&pan);
                ok(rc==DS_OK,"GetPan(primary) failed: %s\n",DXGetErrorString8(rc));
                ok(pan==refpan,"The primary pan changed from %ld to %ld\n",refpan,pan);

                rc=IDirectSoundBuffer_SetVolume(secondary,0);
                ok(rc==DS_OK,"SetVolume(secondary) failed: %s\n",DXGetErrorString8(rc));
                rc=IDirectSoundBuffer_SetPan(secondary,0);
                ok(rc==DS_OK,"SetPan(secondary) failed: %s\n",DXGetErrorString8(rc));
            }
            if (has_duplicate) {
                LPDIRECTSOUNDBUFFER duplicated=NULL;

                /* DSOUND: Error: Invalid source buffer */
                rc=IDirectSound8_DuplicateSoundBuffer(dso,0,0);
                ok(rc==DSERR_INVALIDPARAM,"IDirectSound8_DuplicateSoundBuffer should have failed 0x%lx\n",rc);

                /* DSOUND: Error: Invalid dest buffer */
                rc=IDirectSound8_DuplicateSoundBuffer(dso,secondary,0);
                ok(rc==DSERR_INVALIDPARAM,"IDirectSound8_DuplicateSoundBuffer should have failed 0x%lx\n",rc);

                /* DSOUND: Error: Invalid source buffer */
                rc=IDirectSound8_DuplicateSoundBuffer(dso,0,&duplicated);
                ok(rc==DSERR_INVALIDPARAM,"IDirectSound8_DuplicateSoundBuffer should have failed 0x%lx\n",rc);

                duplicated=NULL;
                rc=IDirectSound8_DuplicateSoundBuffer(dso,secondary,&duplicated);
                ok(rc==DS_OK && duplicated!=NULL,"IDirectSound8_DuplicateSoundBuffer failed to duplicate a secondary buffer 0x%lx\n",rc);

                if (rc==DS_OK && duplicated!=NULL) {
                    ref=IDirectSoundBuffer_Release(secondary);
                    ok(ref==0,"IDirectSoundBuffer_Release secondary has %d references, should have 0\n",ref);
                    secondary=duplicated;
                }
            }

            if (rc==DS_OK && secondary!=NULL) {
                double duration;
                duration=(move_listener || move_sound?4.0:1.0);
                test_buffer8(dso,secondary,0,FALSE,0,FALSE,0,winetest_interactive,duration,has_3dbuffer,listener,move_listener,move_sound);
                ref=IDirectSoundBuffer_Release(secondary);
                ok(ref==0,"IDirectSoundBuffer_Release %s has %d references, should have 0\n",has_duplicate?"duplicated":"secondary",ref);
            }
        }
    }
    if (has_listener) {
        ref=IDirectSound3DListener_Release(listener);
        ok(ref==0,"IDirectSound3dListener_Release listener has %d references, should have 0\n",ref);
    } else {
        ref=IDirectSoundBuffer_Release(primary);
        ok(ref==0,"IDirectSoundBuffer_Release primary has %d references, should have 0\n",ref);
    }

    /* Set the CooperativeLevel back to normal */
    /* DSOUND: Setting DirectSound cooperative level to DSSCL_NORMAL */
    rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_NORMAL);
    ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%0lx\n",rc);

EXIT:
    ref=IDirectSound8_Release(dso);
    ok(ref==0,"IDirectSound8_Release has %d references, should have 0\n",ref);
    if (ref!=0)
        return DSERR_GENERIC;

    return rc;
}

static HRESULT test_primary8(LPGUID lpGuid)
{
    HRESULT rc;
    LPDIRECTSOUND8 dso=NULL;
    LPDIRECTSOUNDBUFFER primary=NULL;
    DSBUFFERDESC bufdesc;
    DSCAPS dscaps;
    int ref, i;

    /* Create the DirectSound object */
    rc=DirectSoundCreate8(lpGuid,&dso,NULL);
    ok(rc==DS_OK,"DirectSoundCreate8 failed: 0x%lx\n",rc);
    if (rc!=DS_OK)
        return rc;

    /* Get the device capabilities */
    ZeroMemory(&dscaps, sizeof(dscaps));
    dscaps.dwSize=sizeof(dscaps);
    rc=IDirectSound8_GetCaps(dso,&dscaps);
    ok(rc==DS_OK,"GetCaps failed: 0x%lx\n",rc);
    if (rc!=DS_OK)
        goto EXIT;

    /* We must call SetCooperativeLevel before calling CreateSoundBuffer */
    /* DSOUND: Setting DirectSound cooperative level to DSSCL_PRIORITY */
    rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_PRIORITY);
    ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%lx\n",rc);
    if (rc!=DS_OK)
        goto EXIT;

    /* Testing the primary buffer */
    primary=NULL;
    ZeroMemory(&bufdesc, sizeof(bufdesc));
    bufdesc.dwSize=sizeof(bufdesc);
    bufdesc.dwFlags=DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN;
    rc=IDirectSound8_CreateSoundBuffer(dso,&bufdesc,&primary,NULL);
    ok(rc==DS_OK && primary!=NULL,"CreateSoundBuffer failed to create a primary buffer: 0x%lx\n",rc);
    if (rc==DS_OK && primary!=NULL) {
        test_buffer8(dso,primary,1,TRUE,0,TRUE,0,winetest_interactive && !(dscaps.dwFlags & DSCAPS_EMULDRIVER),1.0,0,NULL,0,0);
        if (winetest_interactive) {
            LONG volume,pan;

            volume = DSBVOLUME_MAX;
            for (i = 0; i < 6; i++) {
                test_buffer8(dso,primary,1,TRUE,volume,TRUE,0,winetest_interactive && !(dscaps.dwFlags & DSCAPS_EMULDRIVER),1.0,0,NULL,0,0);
                volume -= ((DSBVOLUME_MAX-DSBVOLUME_MIN) / 40);
            }

            pan = DSBPAN_LEFT;
            for (i = 0; i < 7; i++) {
                test_buffer8(dso,primary,1,TRUE,0,TRUE,pan,winetest_interactive && !(dscaps.dwFlags & DSCAPS_EMULDRIVER),1.0,0,0,0,0);
                pan += ((DSBPAN_RIGHT-DSBPAN_LEFT) / 6);
            }
        }
        ref=IDirectSoundBuffer_Release(primary);
        ok(ref==0,"IDirectSoundBuffer_Release primary has %d references, should have 0\n",ref);
    }

    /* Set the CooperativeLevel back to normal */
    /* DSOUND: Setting DirectSound cooperative level to DSSCL_NORMAL */
    rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_NORMAL);
    ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%lx\n",rc);

EXIT:
    ref=IDirectSound8_Release(dso);
    ok(ref==0,"IDirectSound8_Release has %d references, should have 0\n",ref);
    if (ref!=0)
        return DSERR_GENERIC;

    return rc;
}

static HRESULT test_primary_3d8(LPGUID lpGuid)
{
    HRESULT rc;
    LPDIRECTSOUND8 dso=NULL;
    LPDIRECTSOUNDBUFFER primary=NULL;
    DSBUFFERDESC bufdesc;
    DSCAPS dscaps;
    int ref;

    /* Create the DirectSound object */
    rc=DirectSoundCreate8(lpGuid,&dso,NULL);
    ok(rc==DS_OK,"DirectSoundCreate8 failed: 0x%lx\n",rc);
    if (rc!=DS_OK)
        return rc;

    /* Get the device capabilities */
    ZeroMemory(&dscaps, sizeof(dscaps));
    dscaps.dwSize=sizeof(dscaps);
    rc=IDirectSound8_GetCaps(dso,&dscaps);
    ok(rc==DS_OK,"GetCaps failed: 0x%lx\n",rc);
    if (rc!=DS_OK)
        goto EXIT;

    /* We must call SetCooperativeLevel before calling CreateSoundBuffer */
    /* DSOUND: Setting DirectSound cooperative level to DSSCL_PRIORITY */
    rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_PRIORITY);
    ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%lx\n",rc);
    if (rc!=DS_OK)
        goto EXIT;

    primary=NULL;
    ZeroMemory(&bufdesc, sizeof(bufdesc));
    bufdesc.dwSize=sizeof(bufdesc);
    bufdesc.dwFlags=DSBCAPS_PRIMARYBUFFER;
    rc=IDirectSound8_CreateSoundBuffer(dso,&bufdesc,&primary,NULL);
    ok(rc==DS_OK && primary!=NULL,"CreateSoundBuffer failed to create a primary buffer: 0x%lx\n",rc);
    if (rc==DS_OK && primary!=NULL) {
        ref=IDirectSoundBuffer_Release(primary);
        ok(ref==0,"IDirectSoundBuffer_Release primary has %d references, should have 0\n",ref);
        primary=NULL;
        ZeroMemory(&bufdesc, sizeof(bufdesc));
        bufdesc.dwSize=sizeof(bufdesc);
        bufdesc.dwFlags=DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRL3D;
        rc=IDirectSound8_CreateSoundBuffer(dso,&bufdesc,&primary,NULL);
        ok(rc==DS_OK && primary!=NULL,"CreateSoundBuffer failed to create a 3D primary buffer: 0x%lx\n",rc);
        if (rc==DS_OK && primary!=NULL) {
            test_buffer8(dso,primary,1,FALSE,0,FALSE,0,winetest_interactive && !(dscaps.dwFlags & DSCAPS_EMULDRIVER),1.0,0,0,0,0);
            ref=IDirectSoundBuffer_Release(primary);
            ok(ref==0,"IDirectSoundBuffer_Release primary has %d references, should have 0\n",ref);
        }
    }
    /* Set the CooperativeLevel back to normal */
    /* DSOUND: Setting DirectSound cooperative level to DSSCL_NORMAL */
    rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_NORMAL);
    ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%lx\n",rc);

EXIT:
    ref=IDirectSound8_Release(dso);
    ok(ref==0,"IDirectSound8_Release has %d references, should have 0\n",ref);
    if (ref!=0)
        return DSERR_GENERIC;

    return rc;
}

static HRESULT test_primary_3d_with_listener8(LPGUID lpGuid)
{
    HRESULT rc;
    LPDIRECTSOUND8 dso=NULL;
    LPDIRECTSOUNDBUFFER primary=NULL;
    DSBUFFERDESC bufdesc;
    DSCAPS dscaps;
    int ref;

    /* Create the DirectSound object */
    rc=DirectSoundCreate8(lpGuid,&dso,NULL);
    ok(rc==DS_OK,"DirectSoundCreate failed: 0x%lx\n",rc);
    if (rc!=DS_OK)
        return rc;

    /* Get the device capabilities */
    ZeroMemory(&dscaps, sizeof(dscaps));
    dscaps.dwSize=sizeof(dscaps);
    rc=IDirectSound8_GetCaps(dso,&dscaps);
    ok(rc==DS_OK,"GetCaps failed: 0x%lx\n",rc);
    if (rc!=DS_OK)
        goto EXIT;

    /* We must call SetCooperativeLevel before calling CreateSoundBuffer */
    /* DSOUND: Setting DirectSound cooperative level to DSSCL_PRIORITY */
    rc=IDirectSound8_SetCooperativeLevel(dso,get_hwnd(),DSSCL_PRIORITY);
    ok(rc==DS_OK,"SetCooperativeLevel failed: 0x%lx\n",rc);
    if (rc!=DS_OK)
        goto EXIT;
    primary=NULL;
    ZeroMemory(&bufdesc, sizeof(bufdesc));
    bufdesc.dwSize=sizeof(bufdesc);
    bufdesc.dwFlags=DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRL3D;
    rc=IDirectSound8_CreateSoundBuffer(dso,&bufdesc,&primary,NULL);
    ok(rc==DS_OK && primary!=NULL,"CreateSoundBuffer failed to create a 3D primary buffer 0x%lx\n",rc);
    if (rc==DS_OK && primary!=NULL) {
        LPDIRECTSOUND3DLISTENER listener=NULL;
        rc=IDirectSoundBuffer_QueryInterface(primary,&IID_IDirectSound3DListener,(void **)&listener);
        ok(rc==DS_OK && listener!=NULL,"IDirectSoundBuffer_QueryInterface failed to get a 3D listener 0x%lx\n",rc);
        if (rc==DS_OK && listener!=NULL) {
            LPDIRECTSOUNDBUFFER temp_buffer=NULL;

            /* Checking the COM interface */
            rc=IDirectSoundBuffer_QueryInterface(primary, &IID_IDirectSoundBuffer,(LPVOID *)&temp_buffer);
            ok(rc==DS_OK && temp_buffer!=NULL,"IDirectSoundBuffer_QueryInterface failed: 0x%lx\n",rc);
            ok(temp_buffer==primary,"COM interface broken: 0x%08lx != 0x%08lx\n",(DWORD)temp_buffer,(DWORD)primary);
            if (rc==DS_OK && temp_buffer!=NULL) {
                ref=IDirectSoundBuffer_Release(temp_buffer);
                ok(ref==1,"IDirectSoundBuffer_Release has %d references, should have 1\n",ref);

                temp_buffer=NULL;
                rc=IDirectSound3DListener_QueryInterface(listener, &IID_IDirectSoundBuffer,(LPVOID *)&temp_buffer);
                ok(rc==DS_OK && temp_buffer!=NULL,"IDirectSoundBuffer_QueryInterface failed: 0x%lx\n",rc);
                ok(temp_buffer==primary,"COM interface broken: 0x%08lx != 0x%08lx\n",(DWORD)temp_buffer,(DWORD)primary);
                ref=IDirectSoundBuffer_Release(temp_buffer);
                ok(ref==1,"IDirectSoundBuffer_Release has %d references, should have 1\n",ref);

                /* Testing the buffer */
                test_buffer8(dso,primary,1,FALSE,0,FALSE,0,winetest_interactive && !(dscaps.dwFlags & DSCAPS_EMULDRIVER),1.0,0,listener,0,0);
            }

            /* Testing the reference counting */
            ref=IDirectSound3DListener_Release(listener);
            ok(ref==0,"IDirectSound3DListener_Release listener has %d references, should have 0\n",ref);
        }

        /* Testing the reference counting */
        ref=IDirectSoundBuffer_Release(primary);
        ok(ref==0,"IDirectSoundBuffer_Release primary has %d references, should have 0\n",ref);
    }

EXIT:
    ref=IDirectSound8_Release(dso);
    ok(ref==0,"IDirectSound8_Release has %d references, should have 0\n",ref);
    if (ref!=0)
return DSERR_GENERIC;

    return rc;
}

static BOOL WINAPI dsenum_callback(LPGUID lpGuid, LPCSTR lpcstrDescription,
                                   LPCSTR lpcstrModule, LPVOID lpContext)
{
    trace("*** Testing %s - %s\n",lpcstrDescription,lpcstrModule);

    trace("  Testing the primary buffer\n");
    test_primary8(lpGuid);

    trace("  Testing 3D primary buffer\n");
    test_primary_3d8(lpGuid);

    trace("  Testing 3D primary buffer with listener\n");
    test_primary_3d_with_listener8(lpGuid);

    /* Testing secondary buffers */
    test_secondary8(lpGuid,winetest_interactive,0,0,0,0,0,0);
    test_secondary8(lpGuid,winetest_interactive,0,0,0,1,0,0);

    /* Testing 3D secondary buffers */
    test_secondary8(lpGuid,winetest_interactive,1,0,0,0,0,0);
    test_secondary8(lpGuid,winetest_interactive,1,1,0,0,0,0);
    test_secondary8(lpGuid,winetest_interactive,1,1,0,1,0,0);
    test_secondary8(lpGuid,winetest_interactive,1,0,1,0,0,0);
    test_secondary8(lpGuid,winetest_interactive,1,0,1,1,0,0);
    test_secondary8(lpGuid,winetest_interactive,1,1,1,0,0,0);
    test_secondary8(lpGuid,winetest_interactive,1,1,1,1,0,0);
    test_secondary8(lpGuid,winetest_interactive,1,1,1,0,1,0);
    test_secondary8(lpGuid,winetest_interactive,1,1,1,0,0,1);
    test_secondary8(lpGuid,winetest_interactive,1,1,1,0,1,1);

    return 1;
}

static void ds3d8_tests()
{
    HRESULT rc;
    rc=DirectSoundEnumerateA(&dsenum_callback,NULL);
    ok(rc==DS_OK,"DirectSoundEnumerate failed: %ld\n",rc);
}

START_TEST(ds3d8)
{
    HMODULE hDsound;
    FARPROC pFunc;

    CoInitialize(NULL);

    hDsound = LoadLibraryA("dsound.dll");
    if (!hDsound) {
        trace("dsound.dll not found\n");
        return;
    }

    pFunc = (void*)GetProcAddress(hDsound, "DirectSoundCreate8");
    if (!pFunc) {
        trace("ds3d8 test skipped\n");
        return;
    }

    ds3d8_tests();
}
