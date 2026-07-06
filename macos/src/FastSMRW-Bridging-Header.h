// Exposes the FastSMRW core's flat C ABI to Swift. This is the entire native
// surface the app touches: create / set_event_sink / dispatch / destroy /
// version. The app submits commands as JSON and renders events as JSON.
#import "fastsm/capi/fastsm_core.h"
