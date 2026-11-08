/* The PC port's profiler (Tracy) is not on the Wii: its zones are nothing */
#pragma once
#define ZoneScoped
#define ZoneScopedN(name)
#define FrameMark
#define TracyCZoneN(ctx, name, active)
#define TracyCZoneEnd(ctx)
#define TracyCFrameMarkStart(name)
#define TracyCFrameMarkEnd(name)
