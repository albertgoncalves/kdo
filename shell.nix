with import <nixpkgs> {};
mkShell.override { stdenv = llvmPackages_14.stdenv; } {
    buildInputs = [
        glfw3
        libGL
        mold
        renderdoc
        shellcheck
    ];
    APPEND_LIBRARY_PATH = lib.makeLibraryPath [
        glfw
        libGL
    ];
    shellHook = ''
        export LD_LIBRARY_PATH="$APPEND_LIBRARY_PATH:$LD_LIBRARY_PATH"
        . .shellhook
    '';
    hardeningDisable = [ "all" ];
}
