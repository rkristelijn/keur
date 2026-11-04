class Keur < Formula
  desc "Deterministic AI-code guardrail — an anti-slop, vibe-code limiter (git hook)"
  homepage "https://github.com/rkristelijn/keur"
  license "MIT"
  version "0.2.0"

  on_macos do
    # No macOS amd64 asset: Intel Macs run the arm64 build under Rosetta, which
    # mirrors install.sh's own macos-amd64 → arm64 fallback.
    url "https://github.com/rkristelijn/keur/releases/download/v0.2.0/keur-macos-arm64"
    sha256 "4e6f0c490d965a53260fa9a4586e06b4abb91d482ee6a60f95a5f9caea109bcf"

    def install
      bin.install "keur-macos-arm64" => "keur"
    end
  end

  on_linux do
    url "https://github.com/rkristelijn/keur/releases/download/v0.2.0/keur-linux-amd64"
    sha256 "2f441cc6117f83dad5513c6ce1f0fb53a8194bfa77117842339bd20b0ca26a7c"

    def install
      bin.install "keur-linux-amd64" => "keur"
    end
  end

  test do
    assert_match "keur", shell_output("#{bin}/keur version")
  end
end
