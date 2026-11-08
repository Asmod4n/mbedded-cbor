# Debian sid with the Rocq prover (Debian packages coq and libcoq-stdlib) and OCaml, to check the proofs of theories/.
FROM docker.io/library/debian:sid
RUN apt-get update && apt-get install -y --no-install-recommends coq libcoq-stdlib ocaml make && rm -rf /var/lib/apt/lists/*
