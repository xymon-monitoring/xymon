set -e

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential \
  clang \
  perl \
  fping \
  libssl-dev \
  libpcre3-dev \
  librrd-dev \
  libldap2-dev \
  libtirpc-dev

if ! id xymon >/dev/null 2>&1; then
  sudo useradd -r -m -d /home/xymon -s /usr/sbin/nologin xymon
fi

sudo mkdir -p /home/xymon
sudo chown xymon:xymon /home/xymon

