%define TCCL_DIR /opt/tecoai/tccl

Name:           tccl
Version:        %{TCCL_VERSION}
Release:        1%{?dist}
Summary:        tccl
License:        GPL          
Source:         %{name}-%{version}.tar.gz

%description

%prep
%setup -q
rm -rf %{buildroot}/*


mkdir -p %{buildroot}%{TCCL_DIR}
cp -rd ./* %{buildroot}%{TCCL_DIR}
rm -rf %{buildroot}%{TCCL_DIR}/lib/libtccl.so


%pre
rm -rf %{TCCL_DIR}

%post
cd %{TCCL_DIR}/lib
ln -sf libtccl.so.* libtccl.so

chmod -R 777 %{TCCL_DIR}/bin/*
cp %{TCCL_DIR}/bin/* /usr/bin
insmod %{TCCL_DIR}/driver/teco_peer_mem.ko

%clean
rm -rf %{_buildrootdir}/*
rm -rf %{_builddir}/*

%files
%doc
%{TCCL_DIR}/*