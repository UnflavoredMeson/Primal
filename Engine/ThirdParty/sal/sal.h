// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
//
// Minimal stand-in for Microsoft's <sal.h> on non-Windows platforms.
// DirectXMath includes <sal.h>; on Linux these annotations have no meaning, so they expand to nothing.
// Only the annotations used by DirectXMath are defined here.
#pragma once

#ifndef _WIN32

#ifndef _In_
#define _In_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _In_reads_
#define _In_reads_(size)
#endif
#ifndef _In_reads_bytes_
#define _In_reads_bytes_(size)
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Out_writes_
#define _Out_writes_(size)
#endif
#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(size)
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef _Success_
#define _Success_(expr)
#endif
#ifndef _Use_decl_annotations_
#define _Use_decl_annotations_
#endif
#ifndef _Analysis_assume_
#define _Analysis_assume_(expr)
#endif

#endif // !_WIN32
