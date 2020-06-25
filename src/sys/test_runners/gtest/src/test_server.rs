// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_io::{
        self as fio, DirectoryProxy, CLONE_FLAG_SAME_RIGHTS, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_process as fproc,
    fidl_fuchsia_test::{
        self as ftest, Invocation, Result_ as TestResult, RunListenerProxy, Status,
    },
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        future::{abortable, AbortHandle},
        TryStreamExt,
    },
    log::{debug, error, info},
    serde::{Deserialize, Serialize},
    std::{
        path::Path,
        str::from_utf8,
        sync::{Arc, Weak},
    },
    test_runners_lib::{
        cases::TestCaseInfo,
        elf::{Component, FidlError, KernelError, SuiteServer},
        errors::*,
        launch,
        logs::{LogError, LogStreamReader, LogWriter, LoggerStream},
    },
};

/// In `gtest_list_test` output, provides info about individual test cases.
/// Example: For test FOO.Bar, this contains info about Bar.
/// Please refer to documentation of `ListTestResult` for details.
#[derive(Serialize, Deserialize, Debug)]
struct IndividualTestInfo {
    pub name: String,
    pub file: String,
    pub line: u64,
}

/// In `gtest_list_test` output, provides info about individual test suites.
/// Example: For test FOO.Bar, this contains info about FOO.
/// Please refer to documentation of `ListTestResult` for details.
#[derive(Serialize, Deserialize, Debug)]
struct TestSuiteResult {
    pub tests: usize,
    pub name: String,
    pub testsuite: Vec<IndividualTestInfo>,
}

/// Structure of the output of `<test binary> --gtest_list_test`.
///
/// Sample json will look like
/// ```
/// {
/// "tests": 6,
/// "name": "AllTests",
/// "testsuites": [
///    {
///      "name": "SampleTest1",
///      "tests": 2,
///      "testsuite": [
///        {
///          "name": "Test1",
///          "file": "../../src/sys/test_runners/gtest/test_data/sample_tests.cc",
///          "line": 7
///        },
///        {
///          "name": "Test2",
///          "file": "../../src/sys/test_runners/gtest/test_data/sample_tests.cc",
///          "line": 9
///        }
///      ]
///    },
///  ]
///}
///```
#[derive(Serialize, Deserialize, Debug)]
struct ListTestResult {
    pub tests: usize,
    pub name: String,
    pub testsuites: Vec<TestSuiteResult>,
}

/// Provides info about test case failure if any.
#[derive(Serialize, Deserialize, Debug)]
struct Failure {
    pub failure: String,
}

/// Provides info about individual test cases.
/// Example: For test FOO.Bar, this contains info about Bar.
/// Please refer to documentation of `TestOutput` for details.
#[derive(Serialize, Deserialize, Debug)]
struct IndividualTestOutput {
    pub name: String,
    pub status: String,
    pub time: String,
    pub failures: Option<Vec<Failure>>,
}

/// Provides info about individual test suites.
/// Refer to [gtest documentation] for output structure.
/// [gtest documentation]: https://github.com/google/googletest/blob/2002f267f05be6f41a3d458954414ba2bfa3ff1d/googletest/docs/advanced.md#generating-a-json-report
#[derive(Serialize, Deserialize, Debug)]
struct TestSuiteOutput {
    pub name: String,
    pub tests: usize,
    pub failures: usize,
    pub disabled: usize,
    pub time: String,
    pub testsuite: Vec<IndividualTestOutput>,
}

/// Provides info test and the its run result.
/// Example: For test FOO.Bar, this contains info about FOO.
/// Please refer to documentation of `TestSuiteOutput` for details.
#[derive(Serialize, Deserialize, Debug)]
struct TestOutput {
    pub testsuites: Vec<TestSuiteOutput>,
}

/// Opens and reads file defined by `path` in `dir`.
async fn read_file(dir: &DirectoryProxy, path: &Path) -> Result<String, anyhow::Error> {
    // Open the file in read-only mode.
    let result_file_proxy = io_util::open_file(dir, path, OPEN_RIGHT_READABLE)?;
    return io_util::read_file(&result_file_proxy).await;
}

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    /// Cache to store enumerated test names.
    test_list: Option<Arc<Vec<TestCaseInfo>>>,

    /// Directory where test data(json) is written by gtest.
    output_dir_proxy: fio::DirectoryProxy,

    /// Output directory name.
    output_dir_name: String,

    /// Output directory's parent path.
    output_dir_parent_path: String,
}

#[async_trait]
impl SuiteServer for TestServer {
    /// Launches test process and gets test list out. Returns list of tests names in the format
    /// defined by gtests, i.e FOO.Bar.
    /// It only runs enumeration logic once, caches and returns the same result back on subsequent
    /// calls.

    async fn enumerate_tests(
        &mut self,
        test_component: Arc<Component>,
    ) -> Result<Arc<Vec<TestCaseInfo>>, EnumerationError> {
        // Caching
        if self.test_list.is_none() {
            self.test_list = Some(Arc::new(
                Self::enumerate_tests_internal(
                    test_component,
                    self.test_data_namespace()?,
                    Clone::clone(&self.output_dir_proxy),
                )
                .await?,
            ));
        }

        Ok(self.test_list.as_ref().expect("Unexpected caching error").clone())
    }

    // TODO(45852): Support disabled tests.
    // TODO(45853): Support test stdout, or devise a mechanism to replace it.
    async fn run_tests(
        &self,
        invocations: Vec<Invocation>,
        component: Arc<Component>,
        run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        for invocation in invocations {
            self.run_test(invocation, component.clone(), run_listener).await?;
        }
        Ok(())
    }

    /// Run this server.
    fn run(
        self,
        weak_component: Weak<Component>,
        test_url: &str,
        stream: ftest::SuiteRequestStream,
    ) -> AbortHandle {
        let test_data_name = self.output_dir_name.clone();
        let test_data_parent = self.output_dir_parent_path.clone();
        let test_url = test_url.clone().to_owned();
        let (fut, test_suite_abortable_handle) =
            abortable(self.serve_test_suite(stream, weak_component.clone()));

        fasync::spawn_local(async move {
            match fut.await {
                Ok(result) => {
                    if let Err(e) = result {
                        error!("server failed for test {}: {:?}", test_url, e);
                    }
                }
                Err(e) => debug!("server aborted for test {}: {:?}", test_url, e),
            }
            debug!("Done running server for {}.", test_url);

            // Even if `serve_test_suite` failed, clean local data directory as these files are no
            // longer needed and they are consuming space.
            let test_data_dir = io_util::open_directory_in_namespace(
                &test_data_parent,
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            )
            .expect("Cannot open data directory");
            if let Err(e) = files_async::remove_dir_recursive(&test_data_dir, &test_data_name).await
            {
                error!(
                    "error deleting temp data dir '{}/{}': {:?}",
                    test_data_parent, test_data_name, e
                );
            }
        });
        test_suite_abortable_handle
    }
}

impl TestServer {
    /// Creates new test server.
    pub fn new(
        output_dir_proxy: fio::DirectoryProxy,
        output_dir_name: String,
        output_dir_parent_path: String,
    ) -> Self {
        Self {
            test_list: None,
            output_dir_proxy: output_dir_proxy,
            output_dir_name: output_dir_name,
            output_dir_parent_path: output_dir_parent_path,
        }
    }

    fn test_data_namespace(&self) -> Result<fproc::NameInfo, IoError> {
        let client_channnel =
            io_util::clone_directory(&self.output_dir_proxy, CLONE_FLAG_SAME_RIGHTS)
                .map_err(IoError::CloneProxy)?
                .into_channel()
                .map_err(|_| FidlError::ProxyToChannel)
                .unwrap()
                .into_zx_channel();

        Ok(fproc::NameInfo { path: "/test_data".to_owned(), directory: client_channnel.into() })
    }

    async fn run_test(
        &self,
        invocation: Invocation,
        component: Arc<Component>,
        run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        let test = invocation.name.as_ref().ok_or(RunTestError::TestCaseName)?.to_string();
        info!("Running test {}", test);

        let names = vec![self.test_data_namespace()?];
        let my_uuid = uuid::Uuid::new_v4();
        let file_name = format!("test_result-{}.json", my_uuid);
        let test_list_file = Path::new(&file_name);
        let test_list_path = Path::new("/test_data").join(test_list_file);

        let mut args = vec![
            format!("--gtest_filter={}", test),
            format!("--gtest_output=json:{}", test_list_path.display()),
        ];

        args.extend(component.args.clone());

        // run test.
        // Load bearing to hold job guard.
        let (process, _job, mut stdlogger) =
            launch_component_process::<RunTestError>(&component, names, args).await?;

        let (test_logger, log_client) = zx::Socket::create(zx::SocketOpts::DATAGRAM)
            .map_err(KernelError::CreateSocket)
            .unwrap();

        let (case_listener_proxy, listener) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_test::CaseListenerMarker>()
                .map_err(FidlError::CreateProxy)
                .unwrap();

        run_listener
            .on_test_case_started(invocation, log_client, listener)
            .map_err(RunTestError::SendStart)?;
        let test_logger =
            fasync::Socket::from_socket(test_logger).map_err(KernelError::SocketToAsync).unwrap();
        let mut test_logger = LogWriter::new(test_logger);

        let newline = b'\n';

        let prefixes_to_skip = [
            "Note: Google Test filter".as_bytes(),
            " 1 FAILED TEST".as_bytes(),
            "  YOU HAVE 1 DISABLED TEST".as_bytes(),
            "[==========]".as_bytes(),
            "[----------]".as_bytes(),
            "[ RUN      ]".as_bytes(),
            "[  PASSED  ]".as_bytes(),
            "[  FAILED  ]".as_bytes(),
            "[       OK ]".as_bytes(),
        ];
        while let Some(bytes) = stdlogger.try_next().await.map_err(LogError::Read)? {
            if bytes.is_empty() {
                continue;
            }

            let mut iter = bytes.split(|&x| x == newline);

            while let Some(buf) = iter.next() {
                let skip = prefixes_to_skip.iter().any(|p| buf.starts_with(p));
                if !skip {
                    test_logger.write(&buf).await?;
                }
            }
        }

        debug!("Waiting for test to finish: {}", test);

        // wait for test to end.
        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .map_err(KernelError::ProcessExit)
            .unwrap();

        let process_info = process.info().map_err(RunTestError::ProcessInfo)?;

        // gtest returns 0 is test succeeds and 1 if test fails. This will test if test ended abnormally.
        if process_info.return_code != 0 && process_info.return_code != 1 {
            test_logger.write("Test exited abnormally".as_bytes()).await?;

            case_listener_proxy
                .finished(TestResult { status: Some(Status::Failed) })
                .map_err(RunTestError::SendFinish)?;
            return Ok(());
        }

        debug!("Opening output file for {}", test);
        // read test result file.
        let result_str = match read_file(&self.output_dir_proxy, &test_list_file).await {
            Ok(b) => b,
            Err(e) => {
                // TODO(45857): Introduce Status::InternalError.
                test_logger
                    .write_str(format!("Error reading test result:{:?}", IoError::File(e)))
                    .await?;

                case_listener_proxy
                    .finished(TestResult { status: Some(Status::Failed) })
                    .map_err(RunTestError::SendFinish)?;
                return Ok(());
            }
        };

        debug!("parse output file for {}", test);
        let test_list: TestOutput =
            serde_json::from_str(&result_str).map_err(RunTestError::JsonParse)?;
        debug!("parsed output file for {}", test);

        // parse test results.
        if test_list.testsuites.len() != 1 || test_list.testsuites[0].testsuite.len() != 1 {
            // TODO(45857): Introduce Status::InternalError.
            test_logger
                .write_str(
                    "unexpected output, should have received exactly one test result.".to_string(),
                )
                .await?;

            case_listener_proxy
                .finished(TestResult { status: Some(Status::Failed) })
                .map_err(RunTestError::SendFinish)?;
            return Ok(());
        }

        // as we only run one test per iteration result would be always at 0 index in the arrays.
        let test_suite = &test_list.testsuites[0].testsuite[0];
        match &test_suite.failures {
            Some(_failures) => {
                // TODO(53955): re-enable. currently we are getting these logs from test's
                // stdout which we are printing above.
                //for f in failures {
                //   test_logger.write_str(format!("failure: {}\n", f.failure)).await?;
                // }

                case_listener_proxy
                    .finished(TestResult { status: Some(Status::Failed) })
                    .map_err(RunTestError::SendFinish)?;
            }
            None => {
                case_listener_proxy
                    .finished(TestResult { status: Some(Status::Passed) })
                    .map_err(RunTestError::SendFinish)?;
            }
        }
        debug!("test finish {}", test);
        Ok(())
    }

    /// Internal, uncached implementation of `enumerate_tests`.
    async fn enumerate_tests_internal(
        component: Arc<Component>,
        test_data_namespace: fproc::NameInfo,
        output_dir_proxy: fio::DirectoryProxy,
    ) -> Result<Vec<TestCaseInfo>, EnumerationError> {
        let names = vec![test_data_namespace];

        let test_list_file = Path::new("test_list.json");
        let test_list_path = Path::new("/test_data").join(test_list_file);

        let mut args = vec![
            "--gtest_list_tests".to_owned(),
            format!("--gtest_output=json:{}", test_list_path.display()),
        ];
        args.extend(component.args.clone());

        // Load bearing to hold job guard.
        let (process, _job, stdlogger) =
            launch_component_process::<EnumerationError>(&component, names, args).await?;

        // collect stdout in background before waiting for process termination.
        let std_reader = LogStreamReader::new(stdlogger);

        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .map_err(KernelError::ProcessExit)
            .unwrap();

        let process_info = process.info().map_err(KernelError::ProcessInfo).unwrap();
        if process_info.return_code != 0 {
            let logs = std_reader.get_logs().await?;
            // TODO(4610): logs might not be utf8, fix the code.
            let output = from_utf8(&logs)?;
            // TODO(45858): Add a error logger to API so that we can display test stdout logs.
            error!("Failed getting list of tests:\n{}", output);
            return Err(EnumerationError::ListTest);
        }
        let result_str = match read_file(&output_dir_proxy, &test_list_file).await {
            Ok(b) => b,
            Err(e) => {
                let logs = std_reader.get_logs().await?;

                // TODO(4610): logs might not be utf8, fix the code.
                let output = from_utf8(&logs)?;
                error!(
                    "Failed getting list of tests from {}:\n{}",
                    test_list_file.display(),
                    output
                );
                return Err(IoError::File(e).into());
            }
        };

        let test_list: ListTestResult =
            serde_json::from_str(&result_str).map_err(EnumerationError::JsonParse)?;

        let mut tests = Vec::<TestCaseInfo>::with_capacity(test_list.tests);

        for suite in &test_list.testsuites {
            for test in &suite.testsuite {
                let name = format!("{}.{}", suite.name, test.name);
                tests.push(TestCaseInfo { name })
            }
        }

        Ok(tests)
    }
}

/// Convenience wrapper around [`launch::launch_process`].
async fn launch_component_process<E>(
    component: &Component,
    names: Vec<fproc::NameInfo>,
    args: Vec<String>,
) -> Result<(zx::Process, launch::ScopedJob, LoggerStream), E>
where
    E: From<NamespaceError> + From<launch::LaunchError>,
{
    Ok(launch::launch_process(launch::LaunchProcessArgs {
        bin_path: &component.binary,
        process_name: &component.name,
        job: Some(component.job.create_child_job().map_err(KernelError::CreateJob).unwrap()),
        ns: component.ns.clone().map_err(NamespaceError::Clone)?,
        args: Some(args),
        name_infos: Some(names),
        environs: None,
    })
    .await?)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{Context as _, Error},
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_component_runner as fcrunner,
        fidl_fuchsia_test::{RunListenerMarker, RunOptions, SuiteMarker},
        fio::OPEN_RIGHT_WRITABLE,
        fuchsia_runtime::job_default,
        runner::component::ComponentNamespace,
        runner::component::ComponentNamespaceError,
        std::convert::TryFrom,
        std::fs,
        test_runners_test_lib::{collect_listener_event, names_to_invocation, ListenerEvent},
        uuid::Uuid,
    };

    struct TestDataDir {
        dir_name: String,
    }

    impl TestDataDir {
        fn new() -> Result<Self, Error> {
            let dir = format!("/tmp/{}", Uuid::new_v4().to_simple());
            fs::create_dir(&dir).context("cannot create test output directory")?;
            Ok(Self { dir_name: dir })
        }

        fn proxy(&self) -> Result<fio::DirectoryProxy, Error> {
            io_util::open_directory_in_namespace(
                &self.dir_name,
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            )
            .context("Cannot open test data directory")
        }
    }

    impl Drop for TestDataDir {
        fn drop(&mut self) {
            fs::remove_dir_all(&self.dir_name).expect("can't delete temp dir");
        }
    }

    fn create_ns_from_current_ns(
        dir_paths: Vec<(&str, u32)>,
    ) -> Result<ComponentNamespace, ComponentNamespaceError> {
        let mut ns = vec![];
        for (path, permission) in dir_paths {
            let chan = io_util::open_directory_in_namespace(path, permission)
                .unwrap()
                .into_channel()
                .unwrap()
                .into_zx_channel();
            let handle = ClientEnd::new(chan);

            ns.push(fcrunner::ComponentNamespaceEntry {
                path: Some(path.to_string()),
                directory: Some(handle),
            });
        }
        ComponentNamespace::try_from(ns)
    }

    macro_rules! current_job {
        () => {
            job_default().duplicate(zx::Rights::SAME_RIGHTS)?
        };
    }

    fn sample_test_component() -> Result<Arc<Component>, Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        Ok(Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "test.cm".to_owned(),
            binary: "bin/gtest_runner_sample_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        }))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn can_enumerate_sample_test() -> Result<(), Error> {
        let test_data = TestDataDir::new()?;

        let component = sample_test_component()?;

        let mut server =
            TestServer::new(test_data.proxy()?, "some_name".to_owned(), "some_path".to_owned());

        assert_eq!(
            *server.enumerate_tests(component.clone()).await?,
            vec![
                TestCaseInfo { name: "SampleTest1.SimpleFail".to_owned() },
                TestCaseInfo { name: "SampleTest1.Crashing".to_owned() },
                TestCaseInfo { name: "SampleTest2.SimplePass".to_owned() },
                TestCaseInfo { name: "SampleFixture.Test1".to_owned() },
                TestCaseInfo { name: "SampleFixture.Test2".to_owned() },
                TestCaseInfo { name: "SampleDisabled.DISABLED_Test1".to_owned() },
                TestCaseInfo { name: "WriteToStdout.TestPass".to_owned() },
                TestCaseInfo { name: "WriteToStdout.TestFail".to_owned() },
                TestCaseInfo { name: "Tests/SampleParameterizedTestFixture.Test/0".to_owned() },
                TestCaseInfo { name: "Tests/SampleParameterizedTestFixture.Test/1".to_owned() },
                TestCaseInfo { name: "Tests/SampleParameterizedTestFixture.Test/2".to_owned() },
                TestCaseInfo { name: "Tests/SampleParameterizedTestFixture.Test/3".to_owned() },
            ]
        );

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn can_enumerate_empty_test_file() -> Result<(), Error> {
        let test_data = TestDataDir::new()?;

        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "test.cm".to_owned(),
            binary: "bin/gtest_runner_no_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server =
            TestServer::new(test_data.proxy()?, "some_name".to_owned(), "some_path".to_owned());

        assert_eq!(*server.enumerate_tests(component.clone()).await?, Vec::<TestCaseInfo>::new());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_huge_tests() -> Result<(), Error> {
        let test_data = TestDataDir::new()?;

        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/huge_test#test.cm".to_owned(),
            name: "test.cm".to_owned(),
            binary: "bin/huge_gtest_runner_example".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server =
            TestServer::new(test_data.proxy()?, "some_name".to_owned(), "some_path".to_owned());

        let mut expected = vec![];
        for i in 0..1000 {
            let name = format!("HugeStress/HugeTest.Test/{}", i);
            expected.push(TestCaseInfo { name });
        }
        assert_eq!(*server.enumerate_tests(component.clone()).await?, expected);

        Ok(())
    }

    async fn run_tests(invocations: Vec<Invocation>) -> Result<Vec<ListenerEvent>, anyhow::Error> {
        let test_data = TestDataDir::new().context("Cannot create test data")?;
        let component = sample_test_component().context("Cannot create test component")?;
        let weak_component = Arc::downgrade(&component);
        let server = TestServer::new(
            test_data.proxy().context("Cannot create test server")?,
            "some_name".to_owned(),
            "some_path".to_owned(),
        );

        let (run_listener_client, run_listener) =
            fidl::endpoints::create_request_stream::<RunListenerMarker>()
                .context("Failed to create run_listener")?;
        let (test_suite_client, test_suite) =
            fidl::endpoints::create_request_stream::<SuiteMarker>()
                .context("failed to create suite")?;

        let suite_proxy =
            test_suite_client.into_proxy().context("can't convert suite into proxy")?;
        fasync::spawn(async move {
            server
                .serve_test_suite(test_suite, weak_component)
                .await
                .expect("Failed to run test suite")
        });

        fn make_run_options() -> RunOptions {
            let run_options = RunOptions::empty();
            run_options
        }
        suite_proxy
            .run(
                &mut invocations.into_iter().map(|i| i.into()),
                make_run_options(),
                run_listener_client,
            )
            .context("cannot call run")?;

        collect_listener_event(run_listener).await.context("Failed to collect results")
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_multiple_tests() -> Result<(), Error> {
        fuchsia_syslog::init_with_tags(&["gtest_runner_test"]).expect("cannot init logger");
        let events = run_tests(names_to_invocation(vec![
            "SampleTest1.SimpleFail",
            "SampleTest1.Crashing",
            "SampleTest2.SimplePass",
            "Tests/SampleParameterizedTestFixture.Test/2",
        ]))
        .await
        .unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("SampleTest1.SimpleFail"),
            ListenerEvent::finish_test(
                "SampleTest1.SimpleFail",
                TestResult { status: Some(Status::Failed) },
            ),
            ListenerEvent::start_test("SampleTest1.Crashing"),
            ListenerEvent::finish_test(
                "SampleTest1.Crashing",
                TestResult { status: Some(Status::Failed) },
            ),
            ListenerEvent::start_test("SampleTest2.SimplePass"),
            ListenerEvent::finish_test(
                "SampleTest2.SimplePass",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::start_test("Tests/SampleParameterizedTestFixture.Test/2"),
            ListenerEvent::finish_test(
                "Tests/SampleParameterizedTestFixture.Test/2",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_no_test() -> Result<(), Error> {
        let events = run_tests(vec![]).await.unwrap();

        let expected_events = vec![ListenerEvent::finish_all_test()];

        assert_eq!(expected_events, events);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_one_test() -> Result<(), Error> {
        let events = run_tests(names_to_invocation(vec!["SampleTest2.SimplePass"])).await.unwrap();

        let expected_events = vec![
            ListenerEvent::start_test("SampleTest2.SimplePass"),
            ListenerEvent::finish_test(
                "SampleTest2.SimplePass",
                TestResult { status: Some(Status::Passed) },
            ),
            ListenerEvent::finish_all_test(),
        ];

        assert_eq!(expected_events, events);

        Ok(())
    }
}
