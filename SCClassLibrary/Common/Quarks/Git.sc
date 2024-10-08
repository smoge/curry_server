Git {
	var <>localPath, >url, tag, sha, remoteLatest, defaultBranchName, tags;
	classvar gitIsInstalled;

	*isGit { |localPath|
		^File.exists(localPath +/+ ".git")
	}
	*new { |localPath|
		^super.new.localPath_(localPath)
	}
	clone { |url|
		this.git([
			"clone",
			url,
			thisProcess.platform.formatPathForCmdLine(localPath)
		], false);
		this.url = url;
	}
	pull {
		this.git(["pull", "origin", this.defaultBranchName ? ""])
	}
	checkout { |refspec|
		this.git(["checkout", refspec])
	}
	fetch {
		tags = remoteLatest = nil;
		this.git(["fetch"]);
	}
	isDirty {
		^this.git(["--no-pager diff HEAD --"]).size != 0
	}
	url {
		^url ?? {
			url = this.remote;
		}
	}
	branch {
		var out = this.git(["rev-parse --abbrev-ref HEAD"]);
		^if(out.size > 0) { out } { nil }
	}
	remote {
		// detect origin of repo or nil
		// origin	https://github.com/supercollider-quarks/MathLib (fetch)
		// origin	https://github.com/supercollider-quarks/MathLib (push)
		// problem: if more than one remote then this will just detect the first
		// should favor 'origin' if more than one
		var out = this.git(["remote -v"]),
			match = out.findRegexp("^[a-zA-Z0-9]+\t([^ ]+) \\(fetch\\)");
		if(match.size > 0, {
			^match[1][1]
		});
		^nil
	}
	remoteAsHttpUrl {
		var giturl = this.url;
		var hosturl, path;

		if(giturl.notNil, {
			if(giturl.beginsWith("git@"), {
				#hosturl, path = giturl.split($:);
				^"https://%/%".format(hosturl.split($@).last, path)
			});
			if(giturl.beginsWith("git:"), {
				^("https:" ++ giturl.copyToEnd(4))
			});
			^giturl;
		});
		^nil
	}
	refspec {
		^this.tag ?? { this.sha }
	}
	tag {
		// find what tag is currently checked out
		var out, match;
		^tag ?? {
			out = this.git(["--no-pager log --pretty=format:'%d' --abbrev-commit --date=short -1"]);
			match = out.findRegexp("tag: ([^ ,\)]+)");
			if(match.size > 0, {
				tag = "tags/" ++ match[1][1]
			});
			tag
		}
	}
	sha {
		// find what hash is currently checked out
		^sha ?? {
			sha = this.git(["rev-parse HEAD"]);
		}
	}
	defaultBranchName {
		//find out what the remote default branch name is (ie master or main)
		^defaultBranchName ?? {
			defaultBranchName = this.git(["symbolic-ref refs/remotes/origin/HEAD --short"]).split($/)[1]
		}
	}
	remoteLatest {
		// find what the latest commit on the remote is
		^remoteLatest ?? {
			remoteLatest = this.git(["rev-parse origin" +/+ ( this.defaultBranchName ? "" )]);
		}
	}
	tags {
		var raw;
		// all tags
		// only includes ones that have been fetched from remote
		^tags ?? {
			if(thisProcess.platform.name !== 'windows', {
				raw = this.git(["for-each-ref --format='%(refname)' --sort=taggerdate refs/tags"]);
			}, {
				raw = this.git(["for-each-ref --format=%(refname) --sort=taggerdate refs/tags"]);
			});
			tags = raw.split(Char.nl)
				.select({ |t| t.size != 0 })
				.reverse()
				.collect({ |t| t.copyToEnd(10) });
		}
	}
	shaForTag { |tag|
		var out = this.git(["rev-list", tag, "--max-count=1"]);
		^out.copyFromStart(39)
	}
	git { |args, cd=true|
		var cmd, result="";

		if(cd, {
			cmd = ["cd", thisProcess.platform.formatPathForCmdLine(localPath), "&&", "git"];
		},{
			cmd = ["git"];
		});
		cmd = (cmd ++ args).join(" ");
		// this blocks the app thread
		Pipe.callSync(cmd, { |res|
			result = res;
		}, {
			Git.checkForGit();
		});
		^result;
	}
	*checkForGit {
		var gitFind;
		if(gitIsInstalled.isNil) {
			if(thisProcess.platform.name !== 'windows') {
				gitFind = "which git";
			} {
				gitFind = "where git";
			};
			Pipe.callSync(gitFind, {
				gitIsInstalled = true;
			}, { arg error;
				"Quarks requires git to be installed. Follow the installation instructions at <https://git-scm.com>.".error;
				gitIsInstalled = false;
			});
		};
		^gitIsInstalled
	}
}
