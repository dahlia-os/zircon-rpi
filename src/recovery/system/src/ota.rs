// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{setup::DevhostConfig, storage::Storage},
    anyhow::{Context, Error},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_paver as fpaver, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::prelude::*,
    hyper::Uri,
    isolated_ota::download_and_apply_update,
    serde_json::{json, Value},
    std::{fs::File, str::FromStr},
};

const BOARD_NAME_PATH: &str = "/config/build-info/board";

enum PaverType {
    /// Use the real paver.
    Real,
    /// Use a fake paver, which can be connected to using the given connector.
    #[allow(dead_code)]
    Fake { connector: ClientEnd<DirectoryMarker> },
}

enum OtaType {
    /// Ota from a devhost.
    Devhost { cfg: DevhostConfig },
    /// Ota from a well-known location. TODO(simonshields): implement this.
    WellKnown,
}

enum BoardName {
    /// Use board name from /config/build-info.
    BuildInfo,
    /// Override board name with given value.
    #[allow(dead_code)]
    Override { name: String },
}

enum StorageType {
    /// Use real storage (i.e. wipe disk and use the real FVM)
    Real,
    /// Use the given DirectoryMarker for blobfs, and the given path for minfs.
    #[allow(dead_code)]
    Fake { blobfs_root: ClientEnd<DirectoryMarker>, minfs_path: String },
}

/// Helper for constructing OTAs.
pub struct OtaEnvBuilder {
    board_name: BoardName,
    ota_type: OtaType,
    paver: PaverType,
    ssl_certificates: String,
    storage_type: StorageType,
}

impl OtaEnvBuilder {
    pub fn new() -> Self {
        OtaEnvBuilder {
            board_name: BoardName::BuildInfo,
            ota_type: OtaType::WellKnown,
            paver: PaverType::Real,
            ssl_certificates: "/config/ssl".to_owned(),
            storage_type: StorageType::Real,
        }
    }

    #[cfg(test)]
    /// Override the board name for this OTA.
    pub fn board_name(mut self, name: &str) -> Self {
        self.board_name = BoardName::Override { name: name.to_owned() };
        self
    }

    #[cfg(test)]
    /// Use the given blobfs root and path for minfs.
    pub fn fake_storage(
        mut self,
        blobfs_root: ClientEnd<DirectoryMarker>,
        minfs_path: String,
    ) -> Self {
        self.storage_type = StorageType::Fake { blobfs_root, minfs_path };
        self
    }

    /// Use the given |DevhostConfig| to run an OTA.
    pub fn devhost(mut self, cfg: DevhostConfig) -> Self {
        self.ota_type = OtaType::Devhost { cfg };
        self
    }

    #[cfg(test)]
    /// Use the given connector to connect to a paver service.
    pub fn fake_paver(mut self, connector: ClientEnd<DirectoryMarker>) -> Self {
        self.paver = PaverType::Fake { connector };
        self
    }

    #[cfg(test)]
    /// Use the given path for SSL certificates.
    pub fn ssl_certificates(mut self, path: &str) -> Self {
        self.ssl_certificates = path.to_owned();
        self
    }

    /// Takes a devhost config, and converts into a pkg-resolver friendly format.
    /// Returns SSH authorized keys and a |File| representing a directory with the repository
    /// configuration in it.
    async fn get_devhost_config(
        &self,
        cfg: &DevhostConfig,
    ) -> Result<(Option<String>, File), Error> {
        // Get the repository information from the devhost (including keys and repo URL).
        let client = fuchsia_hyper::new_client();
        let response = client
            .get(Uri::from_str(&cfg.url).context("Bad URL")?)
            .await
            .context("Fetching config from devhost")?;
        let body = response
            .into_body()
            .try_fold(Vec::new(), |mut vec, b| async move {
                vec.extend(b);
                Ok(vec)
            })
            .await
            .context("into body")?;
        let repo_info: Value = serde_json::from_slice(&body).context("Failed to parse JSON")?;

        // Convert into a pkg-resolver friendly format.
        let config_for_resolver = json!({
            "version": "1",
            "content": [
            {
                "repo_url": "fuchsia-pkg://fuchsia.com",
                "root_version": 1,
                "root_threshold": 1,
                "root_keys": repo_info["RootKeys"],
                "mirrors":[{
                    "mirror_url": repo_info["RepoURL"],
                    "subscribe": true
                }],
                "update_package_url": null
            }
            ]
        });

        // Set up a repo configuration folder for the resolver, and write out the config.
        let tempdir = tempfile::tempdir().context("tempdir")?;
        let file = tempdir.path().join("devhost.json");
        let tmp_file = File::create(file).context("Creating file")?;
        serde_json::to_writer(tmp_file, &config_for_resolver).context("Writing JSON")?;

        Ok((
            Some(cfg.authorized_keys.clone()),
            File::open(tempdir.into_path()).context("Opening tmpdir")?,
        ))
    }

    /// Wipe the system's disk and mount the clean minfs/blobfs partitions.
    async fn init_real_storage(
        &self,
    ) -> Result<(Option<Storage>, ClientEnd<DirectoryMarker>, String), Error> {
        let mut storage = Storage::new().await.context("initialising storage")?;
        let blobfs_root = storage.get_blobfs().context("Opening blobfs")?;
        storage.mount_minfs().context("Mounting minfs")?;

        Ok((Some(storage), blobfs_root, "/m".to_owned()))
    }

    /// Construct an |OtaEnv| from this |OtaEnvBuilder|.
    pub async fn build(self) -> Result<OtaEnv, Error> {
        let (authorized_keys, repo_dir) = match &self.ota_type {
            OtaType::Devhost { cfg } => {
                self.get_devhost_config(cfg).await.context("Getting devhost config")?
            }
            OtaType::WellKnown => panic!("Not implemented"),
        };

        let ssl_certificates =
            File::open(&self.ssl_certificates).context("Opening SSL certificate folder")?;

        let (storage, blobfs_root, minfs_root_path) =
            if let StorageType::Fake { blobfs_root, minfs_path } = self.storage_type {
                (None, blobfs_root, minfs_path)
            } else {
                self.init_real_storage().await?
            };

        let paver_connector = match self.paver {
            PaverType::Real => {
                let (paver_connector, remote) = zx::Channel::create()?;
                let mut paver_fs = ServiceFs::new();
                paver_fs.add_proxy_service::<fpaver::PaverMarker, _>();
                paver_fs.serve_connection(remote).context("Failed to serve on channel")?;
                fasync::spawn(paver_fs.collect());
                ClientEnd::from(paver_connector)
            }
            PaverType::Fake { connector } => connector,
        };

        let board_name = match self.board_name {
            BoardName::BuildInfo => {
                std::fs::read_to_string(BOARD_NAME_PATH).context("Reading board name")?
            }
            BoardName::Override { name } => name,
        };

        Ok(OtaEnv {
            authorized_keys,
            blobfs_root,
            board_name,
            minfs_root_path,
            paver_connector,
            repo_dir,
            ssl_certificates,
            _storage: storage,
        })
    }
}

pub struct OtaEnv {
    authorized_keys: Option<String>,
    blobfs_root: ClientEnd<DirectoryMarker>,
    board_name: String,
    minfs_root_path: String,
    paver_connector: ClientEnd<DirectoryMarker>,
    repo_dir: File,
    ssl_certificates: File,
    _storage: Option<Storage>,
}

impl OtaEnv {
    /// Run the OTA, targeting the given channel and reporting the given version
    /// as the current system version.
    pub async fn do_ota(self, channel: &str, version: &str) -> Result<(), Error> {
        download_and_apply_update(
            self.blobfs_root,
            self.paver_connector,
            self.repo_dir,
            self.ssl_certificates,
            channel,
            &self.board_name,
            version,
            None,
        )
        .await
        .context("Installing OTA")?;

        if let Some(keys) = self.authorized_keys {
            OtaEnv::install_ssh_certificates(&self.minfs_root_path, &keys)
                .context("Installing SSH authorized keys")?;
        }
        Ok(())
    }

    /// Install SSH certificates into the target minfs.
    fn install_ssh_certificates(minfs_root: &str, keys: &str) -> Result<(), Error> {
        std::fs::create_dir(&format!("{}/ssh", minfs_root)).context("Creating ssh dir")?;
        std::fs::write(&format!("{}/ssh/authorized_keys", minfs_root), keys)
            .context("Writing authorized_keys")?;
        Ok(())
    }
}

/// Run an OTA from a development host. Returns when the system and SSH keys have been installed.
pub async fn run_devhost_ota(cfg: DevhostConfig) -> Result<(), Error> {
    let ota_env = OtaEnvBuilder::new().devhost(cfg).build().await.context("Creating OTA env")?;
    ota_env.do_ota("devhost", "20200101.1.1").await
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        blobfs_ramdisk::BlobfsRamdisk,
        fidl_fuchsia_pkg_ext::RepositoryKey,
        fuchsia_async as fasync,
        fuchsia_pkg_testing::{serve::UriPathHandler, Package, PackageBuilder, RepositoryBuilder},
        futures::future::{ready, BoxFuture},
        hyper::{header, Body, Response, StatusCode},
        mock_paver::MockPaverServiceBuilder,
        std::{
            collections::{BTreeSet, HashMap},
            path::Path,
            sync::{Arc, Mutex},
        },
    };

    /// Wrapper around a ramdisk blobfs and a temporary directory
    /// we pretend is minfs.
    struct FakeStorage {
        blobfs: BlobfsRamdisk,
        minfs: tempfile::TempDir,
    }

    impl FakeStorage {
        pub fn new() -> Result<Self, Error> {
            let minfs = tempfile::tempdir().context("making tempdir")?;
            let blobfs = BlobfsRamdisk::start().context("launching blobfs")?;
            Ok(FakeStorage { blobfs, minfs })
        }

        /// Get all the blobs inside the blobfs.
        pub fn list_blobs(&self) -> Result<BTreeSet<fuchsia_merkle::Hash>, Error> {
            self.blobfs.list_blobs()
        }

        /// Get the blobfs root directory.
        pub fn blobfs_root(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
            self.blobfs.root_dir_handle()
        }

        /// Get the path to be used for minfs.
        pub fn minfs_path(&self) -> String {
            self.minfs.path().to_string_lossy().into_owned()
        }
    }

    /// This wraps a |FakeConfigHandler| in an |Arc|
    /// so that we can implement UriPathHandler for it.
    struct FakeConfigArc {
        pub arc: Arc<FakeConfigHandler>,
    }

    /// This class is used to provide the '/config.json' endpoint
    /// which the OTA process uses to discover information about the devhost repo.
    struct FakeConfigHandler {
        repo_keys: BTreeSet<RepositoryKey>,
        address: Mutex<String>,
    }

    impl FakeConfigHandler {
        pub fn new(repo_keys: BTreeSet<RepositoryKey>) -> Arc<Self> {
            Arc::new(FakeConfigHandler { repo_keys, address: Mutex::new("unknown".to_owned()) })
        }

        pub fn set_repo_address(self: Arc<Self>, addr: String) {
            let mut val = self.address.lock().unwrap();
            *val = addr;
        }
    }

    impl UriPathHandler for FakeConfigArc {
        fn handle(
            &self,
            uri_path: &Path,
            response: Response<Body>,
        ) -> BoxFuture<'_, Response<Body>> {
            if uri_path.to_string_lossy() != "/config.json" {
                return ready(response).boxed();
            }

            // We don't expect any contention on this lock: we only need it
            // because the test doesn't know the address of the server until it's running.
            let val = self.arc.address.lock().unwrap();
            if *val == "unknown" {
                panic!("Expected address to be set!");
            }

            // This emulates the format returned by `pm serve` running on a devhost.
            let config = json!({
                "ID": &*val,
                "RepoURL": &*val,
                "BlobRepoURL": format!("{}/blobs", val),
                "RatePeriod": 60,
                "RootKeys": self.arc.repo_keys,
                "StatusConfig": {
                    "Enabled": true
                },
                "Auto": true,
                "BlobKey": null,
            });

            let json_str = serde_json::to_string(&config).context("Serializing JSON").unwrap();
            let response = Response::builder()
                .status(StatusCode::OK)
                .header(header::CONTENT_LENGTH, json_str.len())
                .body(Body::from(json_str))
                .unwrap();

            ready(response).boxed()
        }
    }

    const EMPTY_REPO_PATH: &str = "/pkg/empty-repo";
    const TEST_SSL_CERTS: &str = "/pkg/data/ssl";

    /// Represents an OTA that is yet to be run.
    struct TestOtaEnv {
        authorized_keys: Option<String>,
        images: HashMap<String, Vec<u8>>,
        packages: Vec<Package>,
        storage: FakeStorage,
    }

    impl TestOtaEnv {
        pub fn new() -> Result<Self, Error> {
            Ok(TestOtaEnv {
                authorized_keys: None,
                images: HashMap::new(),
                packages: vec![],
                storage: FakeStorage::new().context("Starting fake storage")?,
            })
        }

        /// Add a package to be installed by this OTA.
        pub fn add_package(mut self, p: Package) -> Self {
            self.packages.push(p);
            self
        }

        /// Add an image to include in the update package for this OTA.
        pub fn add_image(mut self, name: &str, data: &str) -> Self {
            self.images.insert(name.to_owned(), data.to_owned().into_bytes());
            self
        }

        /// Set the authorized keys to be installed by the OTA.
        pub fn authorized_keys(mut self, keys: &str) -> Self {
            self.authorized_keys = Some(keys.to_owned());
            self
        }

        /// Generates the packages.json file for the update package.
        fn generate_packages_list(&self) -> String {
            let package_urls: Vec<String> = self
                .packages
                .iter()
                .map(|p| {
                    format!(
                        "fuchsia-pkg://fuchsia.com/{}/0?hash={}",
                        p.name(),
                        p.meta_far_merkle_root()
                    )
                })
                .collect();
            let packages = json!({
                "version": 1,
                "content": package_urls,
            });
            serde_json::to_string(&packages).unwrap()
        }

        /// Build an update package from the list of packages and images included
        /// in this update.
        async fn make_update_package(&self) -> Result<Package, Error> {
            let mut update = PackageBuilder::new("update")
                .add_resource_at("packages.json", self.generate_packages_list().as_bytes());

            for (name, data) in self.images.iter() {
                update = update.add_resource_at(name, data.as_slice());
            }

            update.build().await.context("Building update package")
        }

        /// Run the OTA.
        pub async fn run_ota(&mut self) -> Result<(), Error> {
            let update = self.make_update_package().await?;
            // Create the repo.
            let repo = Arc::new(
                self.packages
                    .iter()
                    .fold(
                        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&update),
                        |repo, package| repo.add_package(package),
                    )
                    .build()
                    .await
                    .context("Building repo")?,
            );
            // We expect the update package to be in blobfs, so add it to the list of packages.
            self.packages.push(update);

            // Add a hook to handle the config.json file, which is exposed by
            // `pm serve` to enable autoconfiguration of repositories.
            let request_handler = FakeConfigHandler::new(repo.root_keys());
            let served_repo = Arc::clone(&repo)
                .server()
                .uri_path_override_handler(FakeConfigArc { arc: Arc::clone(&request_handler) })
                .start()
                .context("Starting repository")?;

            // Configure the address of the repository for config.json
            let url = served_repo.local_url();
            let config_url = format!("{}/config.json", url);
            request_handler.set_repo_address(url);

            // Set up the mock paver.
            let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
            let (paver_connector, remote) = zx::Channel::create()?;
            let mut paver_fs = ServiceFs::new();
            let paver_clone = Arc::clone(&mock_paver);
            paver_fs.add_fidl_service(move |stream: fpaver::PaverRequestStream| {
                fasync::spawn(
                    Arc::clone(&paver_clone)
                        .run_paver_service(stream)
                        .unwrap_or_else(|e| panic!("Failed to run paver: {:?}", e)),
                );
            });
            paver_fs.serve_connection(remote).context("serving paver svcfs")?;
            fasync::spawn(paver_fs.collect());
            let paver_connector = ClientEnd::from(paver_connector);

            // Get the devhost config
            let cfg = DevhostConfig {
                url: config_url,
                authorized_keys: self
                    .authorized_keys
                    .as_ref()
                    .map(|p| p.clone())
                    .unwrap_or("".to_owned()),
            };

            // Build the environment, and do the OTA.
            let ota_env = OtaEnvBuilder::new()
                .board_name("x64")
                .fake_storage(
                    self.storage.blobfs_root().context("Opening blobfs root")?,
                    self.storage.minfs_path(),
                )
                .fake_paver(paver_connector)
                .ssl_certificates(TEST_SSL_CERTS)
                .devhost(cfg)
                .build()
                .await
                .context("Building environment")?;

            ota_env.do_ota("devhost", "20200101.1.1").await.context("Running OTA")?;
            Ok(())
        }

        /// Check that the blobfs contains exactly the blobs we expect it to contain.
        pub async fn check_blobs(&self) {
            let written_blobs = self.storage.list_blobs().expect("Listing blobfs blobs");
            let mut all_package_blobs = BTreeSet::new();
            for package in self.packages.iter() {
                all_package_blobs.append(&mut package.list_blobs().expect("Listing package blobs"));
            }

            assert_eq!(written_blobs, all_package_blobs);
        }

        /// Check that the authorized keys file is what we expect it to be.
        pub async fn check_keys(&self) {
            let keys_path = format!("{}/ssh/authorized_keys", self.storage.minfs_path());
            if let Some(expected) = &self.authorized_keys {
                let result =
                    std::fs::read_to_string(keys_path).expect("Failed to read authorized keys!");
                assert_eq!(&result, expected);
            } else {
                assert_eq!(std::fs::read_to_string(keys_path).unwrap(), "");
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_run_devhost_ota() -> Result<(), Error> {
        let package = PackageBuilder::new("test-package")
            .add_resource_at("data/file1", "Hello, world!".as_bytes())
            .build()
            .await
            .unwrap();
        let mut env = TestOtaEnv::new()?
            .add_package(package)
            .add_image("zbi.signed", "zbi image")
            .add_image("fuchsia.vbmeta", "fuchsia vbmeta")
            .authorized_keys("test authorized keys file!");

        env.run_ota().await?;
        env.check_blobs().await;
        env.check_keys().await;
        Ok(())
    }
}
