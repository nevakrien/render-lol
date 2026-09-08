# Vendored dependencies

CMake builds these local sources directly. It never downloads dependencies.
Both directories were cloned from upstream at these exact revisions:

| Directory | Upstream | Release | Commit |
| --- | --- | --- | --- |
| SDL | https://github.com/libsdl-org/SDL | release-3.2.24 | a8589a84226a6202831a3d49ff4edda4acab9acd |
| quarkphysics | https://github.com/erayzesen/QuarkPhysics | 1.0.7 | 59defd427536cd55952967faa026d7157a11fb9b |

Sources and upstream licenses are unmodified. The original clones' `.git`
directories were removed from the source snapshots. This makes the source trees ordinary
vendored files in the parent repository, rather than accidental Git links
that would omit the sources from a normal clone. Commit the source directories
alongside the app. No submodule initialization is needed.

For an upgrade, clone the desired release into a temporary directory, replace
the corresponding source snapshot, and update this table. Retain upstream
license files. `.clone-metadata` is optional and is never used by the build.
