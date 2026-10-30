"""Test the real FrontendHeap singleton using dirty storage and constructor spies."""
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'Logs/TouchGFX/U3/tests/frontend_storage'
stub = out / 'include/gui_generated/common'
stub.mkdir(parents=True, exist_ok=True)
header = root / 'TouchGFX/gui/include/gui/common/FrontendHeap.hpp'
compiler = shutil.which('g++')
nm = shutil.which('nm')
objdump = shutil.which('objdump')
if not all((compiler, nm, objdump)):
    raise RuntimeError('GNU g++, nm and objdump are required for this native test')

# The base has no virtual functions so its first statement can inspect every byte
# before compiler-inserted vptr stores or any member constructor modifies storage.
# This isolates the real getInstance allocation/initialization sequence, not the
# TouchGFX framework layout or the target linker/SDRAM startup sequence.
(stub / 'FrontendHeapBase.hpp').write_text(r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

void CheckZeroBeforeConstructor();
extern unsigned base_constructions, partition_constructions, model_constructions;
extern unsigned app_constructions, start_calls;
extern bool zero_checked;

namespace touchgfx {
namespace meta {
struct Nil {};
template<class Head, class Tail> struct TypeList {};
template<class List> struct select_type_maxsize { typedef List type; };
template<class Head, class Tail> struct select_type_maxsize<TypeList<Head, Tail>> {
    typedef typename select_type_maxsize<Tail>::type TailMax;
    typedef typename std::conditional<(sizeof(Head) >= sizeof(TailMax)), Head, TailMax>::type type;
};
}
struct AbstractPartition {};
template<class Types, unsigned Count> struct Partition : AbstractPartition {
    unsigned char bytes[sizeof(typename meta::select_type_maxsize<Types>::type) * Count];
    Partition() { ++partition_constructions; bytes[0] = 0x11U; }
};
}

struct Model {
    std::uint32_t marker;
    Model() : marker(0x12345678U) { ++model_constructions; }
};
class FrontendHeap;
struct FrontendApplication {
    std::uint32_t marker;
    Model* model;
    FrontendHeap* heap;
    bool started;
    FrontendApplication(Model& m, FrontendHeap& h)
        : marker(0x87654321U), model(&m), heap(&h), started(false) { ++app_constructions; }
};
struct GeneratedView { unsigned char bytes[512]; };
struct GeneratedPresenter { unsigned char bytes[32]; };
struct GeneratedTransition { unsigned char bytes[16]; };
class FrontendHeapBase {
public:
    typedef GeneratedView MaxGeneratedViewType;
    typedef GeneratedPresenter MaxGeneratedPresenterType;
    typedef GeneratedTransition MaxGeneratedTransitionType;
    std::uint32_t base_marker;
    FrontendHeapBase(touchgfx::AbstractPartition&, touchgfx::AbstractPartition&,
                     touchgfx::AbstractPartition&, FrontendApplication&) {
        CheckZeroBeforeConstructor();
        ++base_constructions;
        base_marker = 0x13579BDFU;
    }
    void gotoStartScreen(FrontendApplication& app) { ++start_calls; app.started = true; }
};
''', encoding='utf-8')

source = out / 'test_frontend_storage.cpp'
source.write_text(r'''
#include <gui/common/FrontendHeap.hpp>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

unsigned base_constructions, partition_constructions, model_constructions;
unsigned app_constructions, start_calls;
bool zero_checked;
static unsigned char* expected_storage;
extern "C" unsigned char* ActualFrontendStorage();

void CheckZeroBeforeConstructor()
{
    CHECK(expected_storage != nullptr);
    CHECK(base_constructions == 0U && partition_constructions == 0U);
    CHECK(model_constructions == 0U && app_constructions == 0U && start_calls == 0U);
    for (std::size_t i = 0U; i < sizeof(FrontendHeap); ++i) {
        if (expected_storage[i] != 0U) {
            std::fprintf(stderr, "FAIL: storage must be zero before constructor, byte %u=%u\n",
                         static_cast<unsigned>(i), static_cast<unsigned>(expected_storage[i]));
            std::exit(1);
        }
    }
    zero_checked = true;
}

int main()
{
    expected_storage = ActualFrontendStorage();
    CHECK(reinterpret_cast<std::uintptr_t>(expected_storage) % 32U == 0U);
    for (std::size_t i = 0U; i < sizeof(FrontendHeap); ++i) expected_storage[i] = 0xA5U;
    for (std::size_t i = 0U; i < sizeof(FrontendHeap); ++i) CHECK(expected_storage[i] == 0xA5U);
    CHECK(!zero_checked && base_constructions == 0U);

    FrontendHeap& first = FrontendHeap::getInstance();
    CHECK(static_cast<void*>(&first) == static_cast<void*>(expected_storage));
    CHECK(zero_checked && base_constructions == 1U && partition_constructions == 3U);
    CHECK(model_constructions == 1U && app_constructions == 1U && start_calls == 1U);
    CHECK(first.base_marker == 0x13579BDFU && first.model.marker == 0x12345678U);
    CHECK(first.app.marker == 0x87654321U && first.app.started);
    CHECK(first.app.model == &first.model && first.app.heap == &first);
    CHECK(first.presenters.bytes[0] == 0x11U && first.views.bytes[0] == 0x11U);
    CHECK(first.transitions.bytes[0] == 0x11U);

    first.base_marker = 0xABCDEF01U;
    first.model.marker = 0xCAFEBABEU;
    first.app.marker = 0xDEADBEEFU;
    first.views.bytes[7] = 0x7BU;
    for (unsigned call = 0U; call < 3U; ++call) {
        FrontendHeap& again = FrontendHeap::getInstance();
        CHECK(&again == &first && again.base_marker == 0xABCDEF01U);
        CHECK(again.model.marker == 0xCAFEBABEU && again.app.marker == 0xDEADBEEFU);
        CHECK(again.views.bytes[7] == 0x7BU);
    }
    CHECK(base_constructions == 1U && partition_constructions == 3U);
    CHECK(model_constructions == 1U && app_constructions == 1U && start_calls == 1U);
    std::puts("PASS: real FrontendHeap clears dirty section storage before one construction and preserves repeated access");
    return 0;
}
''', encoding='utf-8')

flags = [compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O0']
obj = out / 'frontend_storage.o'
subprocess.run([*flags, '-I', str(root / 'TouchGFX/gui/include'),
                '-I', str(out / 'include'), '-c', str(source), '-o', str(obj)],
               check=True, timeout=30)
symbols = subprocess.run([nm, '--defined-only', str(obj)], check=True,
                         capture_output=True, text=True, timeout=10).stdout
storage_symbols = [line.split()[-1] for line in symbols.splitlines()
                   if re.search(r'\b_+ZZN12FrontendHeap11getInstanceEvE7storage$', line)]
if len(storage_symbols) != 1:
    raise AssertionError(f'Expected one real singleton storage symbol, got {storage_symbols}')
sections = subprocess.run([objdump, '-h', str(obj)], check=True,
                          capture_output=True, text=True, timeout=10).stdout
if '.TouchGFXState' not in sections:
    raise AssertionError('Real frontend storage custom section was not emitted')

# An explicit GNU assembler label uses the raw nm symbol, including any native
# PE/i686 leading underscore, without depending on C/C++ decoration conventions.
alias = out / 'frontend_storage_alias.cpp'
alias.write_text('extern unsigned char singleton_storage[] __asm__("'
                 + storage_symbols[0] + '");\n'
                 'extern "C" unsigned char* ActualFrontendStorage() '
                 '{ return singleton_storage; }\n', encoding='utf-8')
exe = out / 'frontend_storage.exe'
subprocess.run([*flags, str(obj), str(alias), '-o', str(exe)], check=True, timeout=30)
subprocess.run([str(exe)], check=True, timeout=10)

# A local negative control proves the assertion depends on the real explicit
# clear. It changes only a generated copy under Logs, never the repository file.
clear = 'std::memset(storage, 0, sizeof storage);'
original = header.read_text(encoding='utf-8')
if original.count(clear) != 1:
    raise AssertionError('Update this test when the real explicit clear changes')
mutation = out / 'without_clear/gui/common/FrontendHeap.hpp'
mutation.parent.mkdir(parents=True, exist_ok=True)
mutation.write_text(original.replace(clear, '/* negative control: no explicit clear */'), encoding='utf-8')
mutant_obj = out / 'frontend_without_clear.o'
mutant_exe = out / 'frontend_without_clear.exe'
subprocess.run([*flags, '-I', str(out / 'without_clear'), '-I', str(out / 'include'),
                '-c', str(source), '-o', str(mutant_obj)], check=True, timeout=30)
subprocess.run([*flags, str(mutant_obj), str(alias), '-o', str(mutant_exe)], check=True, timeout=30)
negative = subprocess.run([str(mutant_exe)], capture_output=True, text=True, timeout=10)
if negative.returncode == 0 or 'storage must be zero before constructor' not in negative.stderr:
    raise AssertionError('Removing the explicit clear must fail at the constructor-entry check')
print('PASS: negative control without explicit clear fails before the first constructor')
