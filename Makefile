# make check: repository rules that need no LLVM tree (see README.md);
# check-knc-gen.sh needs a C compiler.
.PHONY: check
check:
	scripts/check-docs.sh
	scripts/check-series.sh
	scripts/check-distro.sh
	scripts/check-knc-gen.sh
	@for f in scripts/*.sh oracle/*.sh; do bash -n "$$f" || exit 1; done; echo "bash -n: ok"
