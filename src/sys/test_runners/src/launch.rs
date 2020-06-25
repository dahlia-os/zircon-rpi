// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helpers for launching components.

use {
    crate::logs::{create_log_stream, LoggerError, LoggerStream},
    anyhow::Error,
    fidl_fuchsia_process as fproc,
    fuchsia_component::client::connect_to_service,
    fuchsia_runtime as runtime, fuchsia_zircon as zx,
    runner::component::ComponentNamespace,
    runtime::{HandleInfo, HandleType},
    thiserror::Error,
    zx::{AsHandleRef, HandleBased, Process, Task},
};

/// Error encountered while launching a component.
#[derive(Debug, Error)]
pub enum LaunchError {
    #[error("{:?}", _0)]
    Logger(LoggerError),

    #[error("Error connecting to launcher: {:?}", _0)]
    Launcher(Error),

    #[error("{:?}", _0)]
    LoadInfo(runner::component::LaunchError),

    #[error("Error launching process: {:?}", _0)]
    LaunchCall(fidl::Error),

    #[error("Error launching process: {:?}", _0)]
    ProcessLaunch(zx::Status),

    #[error("unexpected error")]
    UnExpectedError,
}

/// Arguments to launch_process.
pub struct LaunchProcessArgs<'a> {
    /// Relative binary path to /pkg.
    pub bin_path: &'a str,
    /// Name of the binary to add to process.
    pub process_name: &'a str,
    ///Job used launch process, if None, a new child of default_job() is used.
    pub job: Option<zx::Job>,
    /// Namespace for binary process to be launched.
    pub ns: ComponentNamespace,
    /// Arguments to binary. Binary name is automatically appended as first argument.
    pub args: Option<Vec<String>>,
    /// Extra names to add to namespace. by default only names from `ns` are added.
    pub name_infos: Option<Vec<fproc::NameInfo>>,
    /// Process environment variables.
    pub environs: Option<Vec<String>>,
}

/// Launches process, assigns a logger as stdout/stderr to launched process.
pub async fn launch_process(
    args: LaunchProcessArgs<'_>,
) -> Result<(Process, ScopedJob, LoggerStream), LaunchError> {
    let launcher = connect_to_service::<fproc::LauncherMarker>().map_err(LaunchError::Launcher)?;

    const STDOUT: u16 = 1;
    const STDERR: u16 = 2;

    let (logger, stdout_handle, stderr_handle) =
        create_log_stream().map_err(LaunchError::Logger)?;

    let mut handle_infos = vec![];

    handle_infos.push(fproc::HandleInfo {
        handle: stdout_handle,
        id: HandleInfo::new(HandleType::FileDescriptor, STDOUT).as_raw(),
    });

    handle_infos.push(fproc::HandleInfo {
        handle: stderr_handle,
        id: HandleInfo::new(HandleType::FileDescriptor, STDERR).as_raw(),
    });

    // Load the component
    let mut launch_info =
        runner::component::configure_launcher(runner::component::LauncherConfigArgs {
            bin_path: args.bin_path,
            name: args.process_name,
            args: args.args,
            ns: args.ns,
            job: args.job,
            handle_infos: Some(handle_infos),
            name_infos: args.name_infos,
            environs: args.environs,
            launcher: &launcher,
        })
        .await
        .map_err(LaunchError::LoadInfo)?;

    let component_job = launch_info
        .job
        .as_handle_ref()
        .duplicate(zx::Rights::SAME_RIGHTS)
        .expect("handle duplication failed!");

    let (status, process) =
        launcher.launch(&mut launch_info).await.map_err(LaunchError::LaunchCall)?;

    let status = zx::Status::from_raw(status);
    if status != zx::Status::OK {
        return Err(LaunchError::ProcessLaunch(status));
    }

    let process = process.ok_or_else(|| LaunchError::UnExpectedError)?;

    Ok((process, ScopedJob::new(zx::Job::from_handle(component_job)), logger))
}

// Structure to guard job and kill it when going out of scope.
pub struct ScopedJob {
    pub object: Option<zx::Job>,
}

impl ScopedJob {
    pub fn new(job: zx::Job) -> Self {
        Self { object: Some(job) }
    }

    /// Return the job back from this scoped object
    pub fn take(mut self) -> zx::Job {
        self.object.take().unwrap()
    }
}

impl Drop for ScopedJob {
    fn drop(&mut self) {
        if let Some(job) = self.object.take() {
            job.kill().ok();
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::logs::{LogStreamReader, LogWriter},
        fuchsia_async as fasync,
        fuchsia_runtime::job_default,
        fuchsia_zircon as zx,
        std::mem::drop,
    };

    #[test]
    fn scoped_job_works() {
        let new_job = job_default().create_child_job().unwrap();
        let job_dup = new_job.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();

        // create new child job, else killing a job has no effect.
        let _child_job = new_job.create_child_job().unwrap();

        // check that job is alive
        let info = job_dup.info().unwrap();
        assert!(!info.exited);
        {
            let _job_about_to_die = ScopedJob::new(new_job);
        }

        // check that job was killed
        let info = job_dup.info().unwrap();
        assert!(info.exited);
    }

    #[test]
    fn scoped_job_take_works() {
        let new_job = job_default().create_child_job().unwrap();
        let raw_handle = new_job.raw_handle();

        let scoped = ScopedJob::new(new_job);

        let ret_job = scoped.take();

        // make sure we got back same job handle.
        assert_eq!(ret_job.raw_handle(), raw_handle);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn log_writer_reader_work() {
        let (sock1, sock2) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let mut log_writer = LogWriter::new(fasync::Socket::from_socket(sock1).unwrap());

        let reader = LoggerStream::new(sock2).unwrap();
        let reader = LogStreamReader::new(reader);

        log_writer.write_str("this is string one.".to_owned()).await.unwrap();
        log_writer.write_str("this is string two.".to_owned()).await.unwrap();
        drop(log_writer);

        let actual = reader.get_logs().await.unwrap();
        let actual = std::str::from_utf8(&actual).unwrap();
        assert_eq!(actual, "this is string one.this is string two.".to_owned());
    }
}
