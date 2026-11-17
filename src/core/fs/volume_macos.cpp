// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/nativefilesystem_posix.h"

#include <sys/mount.h>
#include <sys/param.h>
#include <unistd.h>

namespace ctrm {

VolumeInfo platformVolumeInfo(const QByteArray &folder)
{
    VolumeInfo v;
    const long cs = pathconf(folder.constData(), _PC_CASE_SENSITIVE);
    if (cs == 1)
        v.caseSensitivity = CaseSensitivity::Sensitive;
    else if (cs == 0)
        v.caseSensitivity = CaseSensitivity::Insensitive;
    else
        v.caseSensitivity = CaseSensitivity::Unknown;

    struct statfs sfs;
    if (statfs(folder.constData(), &sfs) == 0) {
        v.fsName = QString::fromUtf8(sfs.f_fstypename);
        const bool apple = v.fsName == u"apfs" || v.fsName == u"hfs";
        v.identityType = apple ? IdentityType::Strong : IdentityType::Weak;
        v.normalizationInsensitive = apple;
    }
    return v;
}

} // namespace ctrm
