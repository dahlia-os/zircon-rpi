import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
import 'package:meta/meta.dart';
import 'package:pedantic/pedantic.dart';
import 'package:test/test.dart';
import 'fake_fx_env.dart';
import 'helpers.dart';

/// Helper which always reports that no package server is running.
class NoPackageServerChecklist implements Checklist {
  @override
  Future<bool> maybeUpdateBasePackages(List<TestBundle> bundles) =>
      Future.value(true);
  @override
  Future<bool> isPackageServerRunning() => Future.value(false);
}

class FuchsiaTestCommandCliFake extends FuchsiaTestCommandCli {
  // Supply this to avoid attempting to read the real file.
  ParsedManifest parsedManifest;
  FuchsiaTestCommandCliFake({
    this.parsedManifest,
  }) : super(
          ['--no-build'],
          usage: (parser) => null,
          fxEnv: FakeFxEnv.shared,
        );

  @override
  FuchsiaTestCommand createCommand() {
    return FuchsiaTestCommandFake(
      testsConfig: testsConfig,
      outputFormatters: [StdOutClosingFormatter()],
      parsedManifest: parsedManifest,
    );
  }
}

class FuchsiaTestCommandFake extends FuchsiaTestCommand {
  // Supply this to avoid attempting to read the real file.
  ParsedManifest parsedManifest;
  FuchsiaTestCommandFake({
    Checklist checklist,
    List<OutputFormatter> outputFormatters,
    TestsConfig testsConfig,
    this.parsedManifest,
  }) : super(
          analyticsReporter: AnalyticsFaker(),
          checklist: checklist ?? AlwaysAllowChecklist(),
          directoryBuilder: (String path, {bool recursive}) => null,
          outputFormatters: outputFormatters,
          testsConfig: testsConfig,
          testRunnerBuilder: (testsConfig) => TestRunner(),
        );
  @override
  Future<void> runTestSuite(ParsedManifest parsedManifest) async {
    emitEvent(BeginningTests());
  }

  @override
  Future<ParsedManifest> readManifest(TestsManifestReader manifestReader) =>
      parsedManifest != null
          ? Future.value(parsedManifest)
          : super.readManifest(manifestReader);
}

class StdOutClosingFormatter extends OutputFormatter {
  @override
  void update(TestEvent event) {
    forcefullyClose();
  }
}

class AnalyticsFaker extends AnalyticsReporter {
  List<List<String>> reportHistory = [];

  @override
  Future<void> report({
    @required String subcommand,
    @required String action,
    String label,
  }) async {
    reportHistory.add([subcommand, action, label]);
  }
}

void main() {
  group('prechecks', () {
    test('raise errors if fx is missing', () {
      var logged = false;
      var cmdCli = FuchsiaTestCommandCli(
        [''],
        usage: (parser) {},
        fxEnv: FakeFxEnv.shared,
      );
      expect(
        () => cmdCli.preRunChecks(
          (Object obj) => logged = true,
        ),
        throwsA(TypeMatcher<MissingFxException>()),
      );
      expect(logged, false);
    });

    test('prints tests when asked', () async {
      String logged = 'empty';
      var calledUsage = false;
      var cmdCli = FuchsiaTestCommandCli(
        ['--printtests'],
        usage: (parser) {
          calledUsage = true;
        },
        fxEnv: FakeFxEnv.shared,
      );
      bool shouldRun = await cmdCli.preRunChecks(
        // '/fake/fx',
        (dynamic output) => logged = output.toString(),
        processLauncher: ProcessLauncher(
          processStarter: returnGivenProcess(
            MockProcess.raw(stdout: 'specific output\n'),
          ),
        ),
      );
      expect(calledUsage, false);
      expect(shouldRun, false);
      // Passing `--printtests` does its thing and exits before `/fake/fx` is
      // validated, which would otherwise throw an exception
      expect(logged, 'specific output\n');
    });

    test('skip update-if-in-base for command tests', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: (testsConfig) => TestRunner(),
      );
      expect(TestBundle.hasDeviceTests(<TestBundle>[]), false);

      var bundles = <TestBundle>[
        cmd.testBundleBuilder(
          TestDefinition.fromJson(
            {
              'environments': [],
              'test': {
                'command': ['some', 'command'],
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'linux',
                'path': 'host_x64/lib_tests',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            },
            buildDir: '/whatever',
          ),
        ),
      ];

      // Command tests are "device" tests for this context
      expect(TestBundle.hasDeviceTests(bundles), false);
    });

    test('skip update-if-in-base for host tests', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: (testsConfig) => TestRunner(),
      );
      expect(TestBundle.hasDeviceTests(<TestBundle>[]), false);

      var bundles = <TestBundle>[
        cmd.testBundleBuilder(
          TestDefinition.fromJson(
            {
              'environments': [],
              'test': {
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'linux',
                'path': 'host_x64/lib_tests',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            },
            buildDir: '/whatever',
          ),
        ),
      ];

      // Outright device test
      expect(TestBundle.hasDeviceTests(bundles), false);
    });

    test('run update-if-in-base for component tests', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: (testsConfig) => TestRunner(),
      );
      expect(TestBundle.hasDeviceTests(<TestBundle>[]), false);

      var bundles = <TestBundle>[
        cmd.testBundleBuilder(
          TestDefinition.fromJson(
            {
              'environments': [],
              'test': {
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'fuchsia',
                'package_url':
                    'fuchsia-pkg://fuchsia.com/pkg-name#meta/component-name.cmx',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            },
            buildDir: '/whatever',
          ),
        ),
      ];

      // Component tests are definitely device tests
      expect(TestBundle.hasDeviceTests(bundles), true);
    });

    test('throws fatal error if package server is not running', () async {
      final outputFormatter = StandardOutputFormatter(
        buffer: OutputBuffer.locMemIO(),
        wrapWith: (str, codes, {bool forScript}) => str,
        isVerbose: false,
        hasRealTimeOutput: false,
      );

      final testsConfig = TestsConfig.fromRawArgs(
        fx: Fx.mock(MockProcess(), FakeFxEnv.shared),
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      final cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        checklist: NoPackageServerChecklist(),
        outputFormatter: outputFormatter,
        testRunnerBuilder: (testsConfig) => FakeTestRunner.passing(),
      );
      final parsedManifest = TestsManifestReader().aggregateTests(
        eventEmitter: (event) => null,
        matchLength: testsConfig.flags.matchLength,
        testBundleBuilder: cmd.testBundleBuilder,
        testsConfig: testsConfig,
        testDefinitions: [
          // Any device test
          TestDefinition(
            buildDir: '/whatever',
            os: 'fuchsia',
            name: 'fuchsia-pkg://fuchsia.com/some_pkg#meta/some_comp.cmx',
            packageUrl: PackageUrl.fromString(
                'fuchsia-pkg://fuchsia.com/some_pkg#meta/some_comp.cmx'),
          ),
        ],
      );
      unawaited(cmd.runTestSuite(parsedManifest));
      await Future(() async {
        // ignore: unused_local_variable
        await for (var event in cmd.stream) {/* no-op */}
      });
      expect(
        outputFormatter.allEvents.any((event) => event is FatalError),
        true,
      );
    });
  });

  group('command cli-wrapper', () {
    test('throws OutputClosedException exception stdout is closed', () async {
      final testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      final testDef = TestDefinition.fromJson(
        {
          'environments': [],
          'test': {
            'cpu': 'x64',
            'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
            'name': 'lib_tests',
            'os': 'fuchsia',
            'package_url':
                'fuchsia-pkg://fuchsia.com/lib-pkg-name#meta/lib-component-name.cmx',
            'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
          }
        },
        buildDir: '/whatever',
      );
      final testBundle = TestBundle.build(
        testsConfig: testsConfig,
        testDefinition: testDef,
        workingDirectory: '/whatever',
        timeElapsedSink: (a, b, c) => null,
        testRunnerBuilder: (config) => FakeTestRunner.passing(),
        directoryBuilder: (path, {bool recursive}) => null,
      );

      var cmdCli = FuchsiaTestCommandCliFake(
        parsedManifest: ParsedManifest(
          testBundles: [testBundle],
          testDefinitions: [testDef],
        ),
      );
      expect(
        cmdCli.run(),
        throwsA(TypeMatcher<OutputClosedException>()),
      );
    });
  });

  group('test analytics are reporting', () {
    var testsConfig = TestsConfig.fromRawArgs(
      rawArgs: [],
      fxEnv: FakeFxEnv.shared,
    );
    test('functions on real test runs', () async {
      var cmd = FuchsiaTestCommand(
        analyticsReporter: AnalyticsFaker(),
        checklist: AlwaysAllowChecklist(),
        directoryBuilder: (String path, {bool recursive}) => null,
        outputFormatters: [
          OutputFormatter.fromConfig(
            testsConfig,
            buffer: OutputBuffer.locMemIO(),
          )
        ],
        testRunnerBuilder: (testsConfig) => FakeTestRunner.passing(),
        testsConfig: TestsConfig.fromRawArgs(
          rawArgs: [],
          fxEnv: FakeFxEnv.shared,
        ),
      );
      var testBundles = <TestBundle>[
        cmd.testBundleBuilder(
          TestDefinition(
            buildDir: '/',
            command: ['asdf'],
            name: 'Big Test',
            os: 'linux',
          ),
        ),
      ];
      await cmd.runTests(testBundles);
      await cmd.cleanUp();
      expect(
        // ignore: avoid_as
        (cmd.analyticsReporter as AnalyticsFaker).reportHistory,
        [
          ['test', 'number', '1']
        ],
      );
    });
    test('is silent on dry runs', () async {
      var cmd = FuchsiaTestCommand(
        analyticsReporter: AnalyticsFaker(),
        checklist: AlwaysAllowChecklist(),
        directoryBuilder: (String path, {bool recursive}) => null,
        outputFormatters: [
          OutputFormatter.fromConfig(
            testsConfig,
            buffer: OutputBuffer.locMemIO(),
          )
        ],
        testRunnerBuilder: (testsConfig) => FakeTestRunner.passing(),
        testsConfig: TestsConfig.fromRawArgs(
          rawArgs: ['--dry'],
          fxEnv: FakeFxEnv.shared,
        ),
      );
      var testBundles = <TestBundle>[
        cmd.testBundleBuilder(
          TestDefinition(
            buildDir: '/',
            command: ['asdf'],
            name: 'Big Test',
            os: 'linux',
          ),
        ),
      ];
      await cmd.runTests(testBundles);
      await cmd.cleanUp();
      expect(
        // ignore: avoid_as
        (cmd.analyticsReporter as AnalyticsFaker).reportHistory,
        hasLength(0),
      );
    });
  });

  group('output directories', () {
    // Helper to assemble fixtures
    List<TestBundle> createFixtures(
      /// Mock builder that should create evidence of having been called
      DirectoryBuilder mockBuilder,
    ) {
      final testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--e2e'],
        fxEnv: FakeFxEnv(
          envReader: EnvReader(
            environment: {
              'FUCHSIA_DEVICE_ADDR': '-dev-addr-',
              'FUCHSIA_SSH_KEY': '-ssh-key-',
              'FUCHSIA_SSH_PORT': '-ssh-port-',
              'FUCHSIA_TEST_OUTDIR': '-test-outdir-',
              'SL4F_HTTP_PORT': '-http-port-',
              'FUCHSIA_IPV4_ADDR': '-ipv4-addr-',
            },
            cwd: '/cwd',
          ),
        ),
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        directoryBuilder: mockBuilder,
        testRunnerBuilder: (testsConfig) => FakeTestRunner.passing(),
      );
      return <TestBundle>[
        cmd.testBundleBuilder(
          TestDefinition.fromJson(
            {
              'environments': [
                {
                  'dimensions': {
                    'device_type': 'asdf',
                  },
                },
              ],
              'test': {
                'cpu': 'x64',
                'label': '//scripts/e2e:e2e_tests(//build/toolchain:host_x64)',
                'name': 'e2e_tests',
                'os': 'linux',
                'path': 'path/to/e2e_tests',
                'runtime_deps': 'host_x64/gen/scripts/e2e/e2e_tests.deps.json'
              }
            },
            buildDir: '/whatever',
          ),
        ),
        cmd.testBundleBuilder(
          TestDefinition.fromJson(
            {
              'environments': [],
              'test': {
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'fuchsia',
                'package_url':
                    'fuchsia-pkg://fuchsia.com/lib-pkg-name#meta/lib-component-name.cmx',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            },
            buildDir: '/whatever',
          ),
        ),
      ];
    }

    test('are created for e2e tests', () async {
      bool builtDirectory = false;
      List<TestBundle> bundles = createFixtures(
        (path, {recursive}) => builtDirectory = true,
      );
      // e2e test
      expect(bundles.first.testDefinition.isE2E, true);
      await bundles.first.run().forEach((event) => null);
      expect(builtDirectory, true, reason: 'because test was e2e');
    });

    test('are not created for non-e2e tests', () async {
      bool builtDirectory = false;
      List<TestBundle> bundles = createFixtures(
        (path, {recursive}) => builtDirectory = true,
      );
      // "lib" test
      await bundles.last.run().forEach((event) => null);
      expect(builtDirectory, false);
    });
  });

  group('build targets', () {
    List<TestBundle> createBundlesFromJson(
      List<Map<String, dynamic>> json,
    ) {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: (testsConfig) => TestRunner(),
      );

      return json
          .map((e) => cmd.testBundleBuilder(
              TestDefinition.fromJson(e, buildDir: FakeFxEnv.shared.outputDir)))
          .toList();
    }

    test('host tests only build the tests path', () async {
      expect(
          TestBundle.calculateMinimalBuildTargets(createBundlesFromJson([
            {
              'environments': [],
              'test': {
                'command': ['some', 'command'],
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'linux',
                'path': 'host_x64/lib_tests',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            }
          ])),
          equals(['host_x64/lib_tests']));
    });

    test('component tests only build updates', () async {
      expect(
          TestBundle.calculateMinimalBuildTargets(createBundlesFromJson([
            {
              'environments': [],
              'test': {
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'fuchsia',
                'package_url':
                    'fuchsia-pkg://fuchsia.com/pkg-name#meta/component-name.cmx',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            }
          ])),
          equals(['updates']));
    });

    test(
        'mixed host and component tests build both updates and the host test path',
        () async {
      expect(
          TestBundle.calculateMinimalBuildTargets(createBundlesFromJson([
            {
              'environments': [],
              'test': {
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'fuchsia',
                'package_url':
                    'fuchsia-pkg://fuchsia.com/pkg-name#meta/component-name.cmx',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            },
            {
              'environments': [],
              'test': {
                'command': ['some', 'command'],
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'linux',
                'path': 'host_x64/lib_tests',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            }
          ])),
          unorderedEquals(['updates', 'host_x64/lib_tests']));
    });

    test('an e2e test forces a full rebuild (default target)', () async {
      expect(
          TestBundle.calculateMinimalBuildTargets(createBundlesFromJson([
            // e2e test
            {
              'environments': [
                {
                  'dimensions': {
                    'device_type': 'asdf',
                  },
                },
              ],
              'test': {
                'cpu': 'x64',
                'label': '//scripts/e2e:e2e_tests(//build/toolchain:host_x64)',
                'name': 'e2e_tests',
                'os': 'linux',
                'path': 'path/to/e2e_tests',
                'runtime_deps': 'host_x64/gen/scripts/e2e/e2e_tests.deps.json'
              }
            },
            // component test
            {
              'environments': [],
              'test': {
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'fuchsia',
                'package_url':
                    'fuchsia-pkg://fuchsia.com/pkg-name#meta/component-name.cmx',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            },
            // host test
            {
              'environments': [],
              'test': {
                'command': ['some', 'command'],
                'cpu': 'x64',
                'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
                'name': 'lib_tests',
                'os': 'linux',
                'path': 'host_x64/lib_tests',
                'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
              }
            }
          ])),
          // calculateMinimalBuildTargets returns null for a full build
          // (default target)
          <String>{});
    });
  });
}
