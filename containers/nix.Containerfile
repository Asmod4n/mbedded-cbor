FROM docker.io/nixos/nix:latest
RUN nix --extra-experimental-features 'nix-command flakes' profile add \
        nixpkgs#cmake nixpkgs#gnumake nixpkgs#doctest \
 && nix --extra-experimental-features 'nix-command flakes' profile add --priority 4 nixpkgs#gcc \
 && nix --extra-experimental-features 'nix-command flakes' profile add --priority 10 nixpkgs#clang \
 && nix-collect-garbage -d
ENV CMAKE_PREFIX_PATH=/root/.nix-profile
