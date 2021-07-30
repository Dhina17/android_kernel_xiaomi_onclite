# Kernel compile script
ROOT_DIR=$(pwd)
DATE_TIME=$(date +"%Y%m%d-%H%M")

function setup () {
    # Install dependencies
    sudo apt install bc bash git-core gnupg build-essential \
        zip curl make automake autogen autoconf autotools-dev libtool shtool python \
        m4 gcc libtool zlib1g-dev flex bison libssl-dev
    
    # Download gcc
    git clone https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9 -b android-10.0.0_r47 --depth=1 gcc
    git clone https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9 -b android-10.0.0_r47 --depth=1 gcc_32

    # Download Clang
    wget https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/tags/android-11.0.0_r37/clang-r365631c.tar.gz
    mkdir clang
    tar xvzf clang-r365631c.tar.gz -C clang
    rm clang-r365631c.tar.gz

    # Clone AnyKernel3
    git clone https://github.com/Dhina17/AnyKernel3

}

function compile() {
    # export the arch
    export ARCH=arm64

    # make clang/gcc PATH available
    PATH=:"${ROOT_DIR}/clang/bin:${PATH}:${ROOT_DIR}/gcc/bin:${PATH}:${ROOT_DIR}/gcc_32/bin:${PATH}"

    # make the config
    make O=out onclite-perf_defconfig

    # Start the build
    make -j$(nproc --all) O=out \
                    ARCH=arm64 \
                    CC=clang \
                    CLANG_TRIPLE=aarch64-linux-gnu- \
                    CROSS_COMPILE=aarch64-linux-android-
}

function make_zip() {
    cp out/arch/arm64/boot/Image.gz-dtb AnyKernel3
    cd AnyKernel3
    zip -r9 Test-Kernel-onclite-${DATE_TIME}.zip *
    cd ..
}

function send_zip() {
    ZIP=$(echo AnyKernel3/*.zip)

    # Send the zip to telegram through bot
    curl -F document=@$ZIP "https://api.telegram.org/bot${BOT_TOKEN}/sendDocument" \
            -F chat_id="${CHAT_ID}" \


}
# Starts here
setup
compile
make_zip
send_zip
