# Third-Party Notices

Floating Damage Informer source code is available under the MIT License in
`LICENSE`.

The compiled SKSE plugin statically links CommonLibSSE-NG 7.2.0, pinned to
commit `7a60f4de794095d7b0f8928d1b930a52e9a7da83`. CommonLibSSE-NG is licensed
under GPL-3.0-or-later with its Modding Exception and GPL-3.0 Linking
Exception. Its license and exception texts are copied into release packages
under `licenses/`.

When distributing the compiled plugin, comply with the CommonLibSSE-NG terms
and make the corresponding source available. The exact CommonLibSSE-NG source
used by this build is available at:

https://github.com/alandtse/CommonLibSSE-NG/tree/7a60f4de794095d7b0f8928d1b930a52e9a7da83

CommonLibSSE-NG's VR support uses OpenVR at commit
`60eb187801956ad277f1cae6680e3a410ee0873b`, distributed under the BSD 3-Clause
License. Its license text is included as `licenses/OpenVR-LICENSE.txt`.

Other dependencies retain their respective licenses. Their license notices are
copied from the vcpkg installation metadata into the release package's
`licenses/` directory.
