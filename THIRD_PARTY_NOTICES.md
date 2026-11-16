# Third-Party Notices

RulesForge first-party code is licensed under the Apache License 2.0.
The components below retain their upstream license terms.

| Component | Repository path | Upstream license | Notes |
| --- | --- | --- | --- |
| SQLite Lemon parser generator | Released vcpkg-cache host SDK | Public-domain dedication | The generator is restored externally, not vendored. |
| MIR | Released vcpkg-cache mir-jit / MIR::MIR | MIT | External static JIT dependency; preserve the package copyright and license. No local modifications. |
| Toad 0.1.3 | Released `Toad::Script` / `Toad::PluginABI` SDK | Apache-2.0 (first-party code) | External runtime dependency; preserve its SDK LICENSE, NOTICE and THIRD_PARTY_NOTICES, including the terms of its dependencies. |

Dependencies provided through Salts, SaltsUtils, DataBind, vcpkg, or another
package manager are not relicensed by RulesForge and remain governed by their
respective upstream licenses.

## MIR

The restored mir-jit package is 1.0.0#3, supplied by the selected vcpkg-cache
release. Its installed copyright file contains the following license:

MIT License

Copyright (c) 2018-2024 Vladimir Makarov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
