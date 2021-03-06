properties ([[$class: 'ParametersDefinitionProperty', parameterDefinitions: [
  [$class: 'StringParameterDefinition', name: 'mbed_os_revision', defaultValue: 'mbed-os-5.4', description: 'Revision of mbed-os to build'],
  [$class: 'BooleanParameterDefinition', name: 'smoke_test', defaultValue: true, description: 'Runs HW smoke tests on Cellular devices']
  ]]])

echo "Run smoke tests: ${params.smoke_test}"

try {
  echo "Verifying build with mbed-os version ${mbed_os_revision}"
  env.MBED_OS_REVISION = "${mbed_os_revision}"
} catch (err) {
  def mbed_os_revision = "master"
  echo "Verifying build with mbed-os version ${mbed_os_revision}"
  env.MBED_OS_REVISION = "${mbed_os_revision}"
}

// Map RaaS instances to corresponding test suites
def raas = [
  "cellular_smoke_ublox_c027.json": "8072"
  // Currently dragonfly is not supported by RAAS, skip it 
  //"cellular_smoke_mts_dragonfly.json": "8072"
  ]

// List of targets with supported modem families
def target_families = [
  "UBLOX": ["UBLOX_C027"]
  ]

// Supported Modems
def targets = [
  "UBLOX_C027"
]

// Map toolchains to compilers
def toolchains = [
  ARM: "armcc",
  GCC_ARM: "arm-none-eabi-gcc",
  IAR: "iar_arm"
  ]

// supported socket tests
def sockets = [
  "udp",
  "tcp"
]

def stepsForParallel = [:]

// Jenkins pipeline does not support map.each, we need to use oldschool for loop
for (int i = 0; i < target_families.size(); i++) {
  for(int j = 0; j < toolchains.size(); j++) {
    for(int k = 0; k < targets.size(); k++) {
      for(int l = 0; l < sockets.size(); l++) {
        def target_family = target_families.keySet().asList().get(i)
        def allowed_target_type = target_families.get(target_family)
        def target = targets.get(k)
        def toolchain = toolchains.keySet().asList().get(j)
        def compilerLabel = toolchains.get(toolchain)
        def stepName = "${target} ${toolchain}"
        def socket = sockets.get(l)

        if(allowed_target_type.contains(target)) {
          stepsForParallel[stepName] = buildStep(target_family, target, compilerLabel, toolchain, socket)
        }
      }
    }
  }
}


def parallelRunSmoke = [:]

// Need to compare boolean against string value
if ( params.smoke_test == true ) {
  // Generate smoke tests based on suite amount
  for(int i = 0; i < raas.size(); i++) {
  	for(int j = 0; j < sockets.size(); j++) {
    	def suite_to_run = raas.keySet().asList().get(i)
    	def raasPort = raas.get(suite_to_run)
    	def socket = sockets.get(j)
    
    	// Parallel execution needs unique step names. Remove .json file ending.
    	def smokeStep = "${raasPort} ${suite_to_run.substring(0, suite_to_run.indexOf('.'))}"
    	parallelRunSmoke[smokeStep] = run_smoke(target_families, raasPort, suite_to_run, toolchains, targets, socket)
    }
  }
}

timestamps {
  parallel stepsForParallel
  parallel parallelRunSmoke
}

def buildStep(target_family, target, compilerLabel, toolchain, socket) {
  return {
    stage ("${target_family}_${target}_${compilerLabel}") {
      node ("${compilerLabel}") {
        deleteDir()
        dir("mbed-os-example-cellular") {
          checkout scm
          def config_file = "mbed_app.json"

          // Activate traces
          //execute("sed -i 's/\"mbed-trace.enable\": false/\"mbed-trace.enable\": true/' ${config_file}")

          //change socket typembed_app.json


          execute("sed -i 's/\"sock-type\": .*/\"sock-type\": \"${socket}\",/' ${config_file}")

          // Set mbed-os to revision received as parameter
          execute ("mbed deploy --protocol ssh")
          //dir ("mbed-os") {
          //  execute ("git checkout ${env.MBED_OS_REVISION}")
          //}

          execute ("mbed compile --build out/${target}_${toolchain}/ -m ${target} -t ${toolchain} -c --app-config ${config_file}")
        }
        stash name: "${target}_${toolchain}_${socket}", includes: '**/mbed-os-example-cellular.bin'
        archive '**/mbed-os-example-cellular.bin'
        step([$class: 'WsCleanup'])
      }
    }
  }
}

def run_smoke(target_families, raasPort, suite_to_run, toolchains, targets, socket) {
  return {
    env.RAAS_USERNAME = "user"
    env.RAAS_PASSWORD = "user"
    // Remove .json from suite name
    def suiteName = suite_to_run.substring(0, suite_to_run.indexOf('.'))
    stage ("smoke_${raasPort}_${suiteName}") {
      //node is actually the type of machine, i.e., mesh-test boild down to linux
      node ("mesh-test") {
        deleteDir()
        dir("mbed-clitest") {
          git "git@github.com:ARMmbed/mbed-clitest.git"
          execute("git checkout master")
          dir("mbed-clitest-suites") {
            git "git@github.com:ARMmbed/mbed-clitest-suites.git"
            execute("git submodule update --init --recursive")
            execute("git all checkout master")
            dir("cellular") {
              execute("git checkout master")
            }
          }
                
          for (int i = 0; i < target_families.size(); i++) {
            for(int j = 0; j < toolchains.size(); j++) {
              for(int k = 0; k < targets.size(); k++) {
            	 def target_family = target_families.keySet().asList().get(i)
                 def allowed_target_type = target_families.get(target_family)
                 def target = targets.get(k)
                 def toolchain = toolchains.keySet().asList().get(j)

                 if(allowed_target_type.contains(target)) {
                    unstash "${target}_${toolchain}_${socket}"
                  }
              	}
            }
          }     
          if ("${suiteName}" == "cellular_smoke_mts_dragonfly")  {
            execute("python clitest.py --suitedir mbed-clitest-suites/suites/ --suite ${suite_to_run} --type hardware --reset hard --raas 62.44.193.186:${raasPort} --tcdir mbed-clitest-suites/cellular  --failure_return_value -vvv -w --log log_${raasPort}_${suiteName}")
          } else {
            execute("python clitest.py --suitedir mbed-clitest-suites/suites/ --suite ${suite_to_run} --type hardware --reset --raas 62.44.193.186:${raasPort} --tcdir mbed-clitest-suites/cellular  --failure_return_value -vvv -w --log log_${raasPort}_${suiteName}")
          }
         archive "log_${raasPort}_${suiteName}/**/*"
        }
      }
    }
  }
}