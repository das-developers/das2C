# das2C style notes

These notes collect the conventions das2C follows, so that someone picking up
the library later does not have to reverse engineer them from the source.
Most of them come down to one idea: das2C is written in C, but it is laid out
as a set of classes.  Naming things the way a class library would makes the
code easier to read, and it lets binding generators and translators for
languages with real classes (Python, JavaScript, C++) map it over with little
hand work.

Nothing here is enforced by a tool.  When the code and this note disagree, the
code is probably older; fix whichever one is wrong.


## Objects and plain values

Types come in two kinds, and the spelling tells you which.

An *object* has identity and a lifetime: it is created, handed around by
pointer, and destroyed.  Objects get CamelCase names that start with `Das`:
`DasDs`, `DasVar`, `DasForm`, `DasAry`.  Shorter is better as long as it
stays readable; `DasFormCplx` and `DasFormPos` are the model.

A *plain value* is a small struct or enum that gets copied around freely and
needs no cleanup: `das_datum`, `das_operand`, `das_val_type`,
`das_form_param_buf`.  These are lower case with a `das_` prefix.


## Creating and destroying objects

The constructor is `new_DasThing()`.  When there is more than one way to
build a thing, the variants add a suffix saying what they build from:
`new_DasIO_file()`, `new_DasIO_socket()`, `new_DasForm_pairs()`.

The destructor is `del_DasThing()`, and it accepts NULL and does nothing, so
cleanup code never needs a guard.

Objects that are shared rather than owned outright are reference counted,
and the count functions are named like the constructor and destructor, not
like methods: `inc_DasThing()`, `dec_DasThing()`, and `ref_DasThing()` to read
the count.  That is deliberate.  They belong to the object's lifecycle, the
same as `new_` and `del_`, and a binding for a garbage collected language
drops the whole lifecycle group at once.  If they were spelled
`DasThing_incRef()` they would look like ordinary methods that ought to be
carried over.  `DasAry` and `DasVar` follow this; `DasGen` and `DasBinOp` do
not yet.

A constructor that fails returns NULL after calling `das_error()`, so the
caller only has to pass the NULL along; the reason has already been reported.


## Methods

A function whose first argument is the object it works on is a method, and
it is named after the class: `DasForm_getParam(pForm, ...)`,
`DasVar_shape(pVar, ...)`.  Pick the class from the type of that first
argument.  The argument itself is `pThis`.

Private methods follow the same rule with a leading underscore:
`_DasForm_paramSet()`, `_DasIO_setMode()`.  This includes the functions
that fill in a virtual table.  A vector formalism's `getParam` slot is
`_DasFormVector_getParam`, because it is a method of `DasFormVector` that
happens to be reached through the base class.  Those functions take the base
pointer, conventionally named `pBase`, and cast it to `pThis` on the first
line.

Private factories have no `pThis` to name them by.  They use the class name
in all lower case: `_dasformvector_new()`, `_dasbinopaffine_new()`.

Everything else is a free function: a lookup table, a unit conversion, a
helper that takes two operands and belongs to neither.  Public free functions
are lower case with a `das_` prefix, such as `das_vsys_toStr()` and
`das_form_lookup()`.  Private ones shared between files through a `_priv.h`
header add a leading underscore, such as `_dasform_prnOrder()`; helpers
used in only one file just need the underscore and a short lower case name,
like `_toDbl()`.

Some accessors are macros today, `DasForm_isKind()` for example.  As the
public structs move behind opaque pointers these will become real functions,
so new accessors should be functions from the start.  (`DasForm_kind()`
replaced a `DasForm_kindStr()` macro that read a vtable constant and so
answered wrongly for generic formalisms; a function can look past that.)


## Keep knowledge where it belongs

Each class owns what is particular about it.  A formalism knows its own
parameters, how to check them, and how to write itself out; nothing outside
`form_vec.c` should need to know what a vector's parameters are.

The warning sign is code outside a class that branches on its concrete kind
to decide what to do, such as a chain of `DasForm_isKind()` tests or a table
of parameter names per kind in a utility program.  That code is correct only
until the next kind is added, and when it goes wrong it usually does so
quietly.  Ask the object instead, through its methods or its virtual table.

Repeating information inside one class is fine when it serves two audiences.
A formalism lists its parameter names once for applications (`paramName`)
and again in its `encode` function for the wire, because each of those wants
something slightly different.  What matters is that both copies sit in the
same file.

The same thinking keeps libdas3 out of property name policy.  That leaves
room for out-of-band signals between a stream writer and one particular
reader, like `cdfName` and `cdfCharLen` for das3_cdf, which the reader acts
on and does not copy into its output.


## Variable names

Local variables and parameters carry a short type prefix:

    p   pointer          pThis, pBuf
    s   string           sName, sVal
    ps  string array     psAttr
    a   fixed array      aShape, aDirs
    n   signed integer   nRank, nRet
    u   unsigned / size  uLen, uComps
    b   boolean          bFixed
    r   double           rScale

It is not applied rigidly.  A loop index is still `i`, and a lone double in
a short block is often just `d`.


## Comments and formatting

Indent with tabs.  The code is C99 and should stay that way.

Comments explain why, not what.  Assume the reader knows C and the standard
library; tell them the constraint they cannot see from the surrounding
lines, the edge case a check exists for, or the reason the obvious approach
was not taken.

Doc comments in public headers are written for application developers.  Keep
the tone level: no upper case words for emphasis, which reads as talking
down to the reader.  Let sentence structure carry the emphasis instead.

Keep comments in plain ASCII.  No em-dashes, curly quotes or other characters
above 127; two hyphens or a semicolon do the job.

The exception is mathematical and scientific symbols, written the way people
actually read them.  `μs` is clearer than `us` or `micro-seconds`, and `θ`,
`φ`, `λ` or `°` say exactly what they mean.  Use them where they help.  The
rule is aimed at typographic decoration, not at notation.
