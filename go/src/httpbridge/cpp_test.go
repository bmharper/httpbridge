package httpbridge

import (
	"bytes"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"os"
	"os/exec"
	"testing"
	"time"
)

const (
	serverFrontPort   = "127.0.0.1:8080"
	serverBackendPort = "127.0.0.1:8081"
	baseUrl           = "http://" + serverFrontPort
)

var cpp_server *exec.Cmd
var cpp_server_out *bytes.Buffer
var front_server *Server
var pingClient http.Client
var requestClient http.Client

// This is useful when you want to launch test-backend.exe from the C++ debugger
// go test httpbridge -external_backend
// Also, make sure to test with at least -cpu 2
var external_backend = flag.Bool("external_backend", false, "Use an externally launched backend server")
var skip_build = flag.Bool("skip_build", false, "Don't build the C++ backend server")

func build_cpp() error {
	if *skip_build {
		return nil
	}
	//cwd, _ := os.Getwd()
	//fmt.Printf("pwd = %v\n", cwd)
	cmd := exec.Command(cpp_test_build[0], cpp_test_build[1:]...)
	cmdOut := &bytes.Buffer{}
	cmd.Stdout = cmdOut
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("Build of test-backend failed: %v\n%v", err, string(cmdOut.Bytes()))
	}
	return nil
}

// process setup
func setup() error {
	flag.Parse()
	if !*external_backend {
		// Compile backend
		if err := build_cpp(); err != nil {
			return err
		}
	}

	return nil
}

// process tear-down
func teardown() {
	kill_cpp(nil)
	//kill_front()
}

func kill_cpp(t *testing.T) {
	if *external_backend {
		return
	}
	if cpp_server != nil {
		fmt.Printf("Stopping cpp server\n")
		if t != nil {
			t.Logf("Stopping cpp server")
		}
		resp, _ := http.Get(baseUrl + "/stop")
		io.Copy(ioutil.Discard, resp.Body)
		resp.Body.Close()
		cpp_server.Wait()
		fmt.Printf("cpp server stopped\n")
		if t != nil {
			if !cpp_server.ProcessState.Success() {
				t.Logf("cpp output:\n%v\n", string(cpp_server_out.Bytes()))
				t.Fatalf("cpp server exited with non-zero exit code")
			}
		}
		//cpp_server.Process.Kill()
		cpp_server = nil
		cpp_server_out = nil
	}
}

//func kill_front() {
//	if front_server != nil {
//		front_server.Stop()
//		front_server = nil
//	}
//}

func restart(t *testing.T) {
	//kill_front()
	kill_cpp(t)

	// Launch HTTP server (front-end).
	// I can't figure out how to properly terminate the net/http serving infrastructure, so we just
	// leave the front-end running.
	if front_server == nil {
		front_server = &Server{}
		front_server.HttpPort = serverFrontPort
		front_server.BackendPort = serverBackendPort
		front_server.Log.Level = LogLevelInfo
		go front_server.ListenAndServe()
	}

	// Launch backend
	if !*external_backend {
		cpp_server = exec.Command(cpp_test_bin)
		cpp_server_out = &bytes.Buffer{}
		cpp_server.Stdout = cpp_server_out
		if err := cpp_server.Start(); err != nil {
			t.Fatalf("Failed to launch cpp backend: %v", err)
		}
	}

	// Wait for backend to come alive
	time.Sleep(10 * time.Millisecond)
	for {
		resp, err := pingClient.Get(baseUrl + "/ping")
		if err == nil {
			io.Copy(ioutil.Discard, resp.Body)
			resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				break
			} else {
				t.Logf("Waiting for backend to come alive... code = %v\n", resp.StatusCode)
			}
		} else {
			t.Logf("Waiting for backend to come alive... error = %v\n", err)
		}
		time.Sleep(50 * time.Millisecond)
	}
}

func doRequest(t *testing.T, method string, url string, body string) *http.Response {
	fmt.Printf("%v %v\n", method, url)
	var bodyReader io.Reader
	if method == "POST" || method == "PUT" {
		bodyReader = bytes.NewReader([]byte(body))
	}
	req, err := http.NewRequest(method, baseUrl+url, bodyReader)
	if err != nil {
		t.Fatalf("%v: Error creating request: %v", url, err)
	}
	resp, err := requestClient.Do(req)
	if err != nil {
		t.Fatalf("%v: Error executing request: %v", url, err)
	}
	return resp
}

func testRequest(t *testing.T, method string, url string, body string, expectCode int, expectBody string) {
	resp := doRequest(t, method, url, body)
	defer resp.Body.Close()
	respBodyB, err := ioutil.ReadAll(resp.Body)
	respBody := string(respBodyB)
	if err != nil {
		t.Fatalf("%v: Error reading response body: %v", url, err)
	}
	if resp.StatusCode != expectCode {
		t.Errorf("%v:\nexpected code (%v)\nreceived code (%v)", url, expectCode, resp.StatusCode)
	}
	if respBody != expectBody {
		t.Errorf("%v:\nexpected (%v)\nreceived (%v)", url, expectBody, respBody)
	}
}

func testPost(t *testing.T, url string, body string, expectCode int, expectBody string) {
	testRequest(t, "POST", url, body, expectCode, expectBody)
}

func testGet(t *testing.T, url string, expectCode int, expectBody string) {
	testRequest(t, "GET", url, "", expectCode, expectBody)
}

func generateBuf(numBytes int) string {
	if numBytes > 1024*1024 {
		fmt.Printf("Generating %v junk buffer\n", numBytes)
	}
	buf := make([]byte, 0, numBytes)
	for i := 0; len(buf) < numBytes; i++ {
		chunk := fmt.Sprintf("%v_", len(buf))
		if len(buf)+len(chunk) > numBytes {
			chunk = chunk[0 : numBytes-len(buf)]
		}
		buf = append(buf, []byte(chunk)...)
	}
	if numBytes > 1024*1024 {
		fmt.Printf("Generation done\n")
	}
	return string(buf)
}

func withCombinations(t *testing.T, f func()) {
	testGet(t, "/control?MaxAutoBufferSize=50000000", 200, "")
	f()
	testGet(t, "/control?MaxAutoBufferSize=0", 200, "")
	f()
}

func TestMain(m *testing.M) {
	if err := setup(); err != nil {
		fmt.Printf("%v\n", err)
		os.Exit(1)
	}
	res := m.Run()
	teardown()
	os.Exit(res)
}

func TestEcho(t *testing.T) {
	restart(t)
	withCombinations(t, func() {
		testPost(t, "/echo", "Hello!", 200, "Hello!")

		smallBuf := generateBuf(5 * 1024)
		testPost(t, "/echo", smallBuf, 200, smallBuf)

		bigBuf := generateBuf(3 * 1024 * 1024)
		testPost(t, "/echo", bigBuf, 200, bigBuf)
	})
}

func TestThreadedBackend(t *testing.T) {
	restart(t)
	nthreads := 8
	done := make(chan bool)
	for i := 0; i < nthreads; i++ {
		go func() {
			for j := 0; j < 2000; j++ {
				msg := fmt.Sprintf("(%v,%v) (Thread: %v, Request number: %v) (%v,%v)", i, j, i, j, i, j)
				testPost(t, "/echo-thread", msg, 200, msg)
			}
			done <- true
		}()
	}
	for i := 0; i < nthreads; i++ {
		<-done
	}
}
