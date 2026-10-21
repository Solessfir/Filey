The Zstandard educational decoder from https://github.com/facebook/zstd/tree/v1.5.7/doc/educational_decoder (BSD, see `LICENSE`), which decompresses the zstd frames of compressed .blend files for the inspector's model previews.

One change: errors jump back through `zstd_error_jump`, which the caller sets with `setjmp`, instead of calling `exit(1)`.
