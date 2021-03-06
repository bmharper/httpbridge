// +build !windows

package httpbridge

var cpp_test_build []string

const cpp_test_bin = "./test-backend"

const tcpBufferSize = 1024 * 1024

func init() {
	root := "../../../"
	cpp_test_build = []string{"gcc", "-g", "-O1", "-I" + root + "cpp/flatbuffers/include", "-pthread", "-std=c++11", root + "cpp/test-backend.cpp", root + "cpp/http-bridge.cpp", "-lstdc++", "-o", "test-backend"}
}
