from conans import ConanFile, CMake


class PromiseTestsConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    requires = (
        "catch2/2.13.0",
        "libcurl/7.79.1"
    )
    generators = "cmake"
    default_options = {}
    build_policy = "missing"

    def imports(self):
        self.copy("*.dll", dst="bin", src="bin")  # From bin to bin
        self.copy("*.dylib*", dst="bin", src="lib")  # From lib to bin

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
