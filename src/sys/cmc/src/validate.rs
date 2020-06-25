// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{cml, one_or_many::OneOrMany},
    cm_json::{self, Error, JsonSchema, CMX_SCHEMA},
    directed_graph::{self, DirectedGraph},
    json5,
    lazy_static::lazy_static,
    serde_json::Value,
    std::{
        collections::{HashMap, HashSet},
        fmt::{self, Display},
        fs::File,
        hash::Hash,
        io::Read,
        iter,
        path::Path,
    },
    valico::json_schema,
};

lazy_static! {
    static ref DEFAULT_EVENT_STREAM_PATH: cml::Path =
        "/svc/fuchsia.sys2.EventStream".parse().unwrap();
}

/// Read in and parse one or more manifest files. Returns an Err() if any file is not valid
/// or Ok(()) if all files are valid.
///
/// The primary JSON schemas are taken from cm_json, selected based on the file extension,
/// is used to determine the validity of each input file. Extra schemas to validate against can be
/// optionally provided.
pub fn validate<P: AsRef<Path>>(
    files: &[P],
    extra_schemas: &[(P, Option<String>)],
) -> Result<(), Error> {
    if files.is_empty() {
        return Err(Error::invalid_args("No files provided"));
    }

    for filename in files {
        validate_file(filename.as_ref(), extra_schemas)?;
    }
    Ok(())
}

/// Read in and parse .cml file. Returns a cml::Document if the file is valid, or an Error if not.
pub fn parse_cml(buffer: &str) -> Result<cml::Document, Error> {
    let document: cml::Document =
        json5::from_str(buffer).map_err(|e| Error::parse(format!("{}", e)))?;
    let mut ctx = ValidationContext {
        document: &document,
        all_children: HashMap::new(),
        all_collections: HashSet::new(),
        all_storage_and_sources: HashMap::new(),
        all_runners: HashSet::new(),
        all_resolvers: HashSet::new(),
        all_environment_names: HashSet::new(),
        all_event_names: HashSet::new(),
    };
    ctx.validate()?;
    Ok(document)
}

/// Read in and parse a single manifest file, and return an Error if the given file is not valid.
/// If the file is a .cml file and is valid, will return Some(cml::Document), and for other valid
/// files returns None.
///
/// Internal single manifest file validation function, used to implement the two public validate
/// functions.
fn validate_file<P: AsRef<Path>>(
    file: &Path,
    extra_schemas: &[(P, Option<String>)],
) -> Result<(), Error> {
    const BAD_EXTENSION: &str = "Input file does not have a component manifest extension \
                                 (.cml or .cmx)";
    let mut buffer = String::new();
    File::open(&file)?.read_to_string(&mut buffer)?;

    // Validate based on file extension.
    let ext = file.extension().and_then(|e| e.to_str());
    match ext {
        Some("cmx") => {
            let v = serde_json::from_str(&buffer)?;
            validate_json(&v, CMX_SCHEMA)?;
            // Validate against any extra schemas provided.
            for extra_schema in extra_schemas {
                let schema = JsonSchema::new_from_file(&extra_schema.0.as_ref())?;
                validate_json(&v, &schema).map_err(|e| match (&e, &extra_schema.1) {
                    (Error::Validate { schema_name, err }, Some(extra_msg)) => Error::Validate {
                        schema_name: schema_name.clone(),
                        err: format!("{}\n{}", err, extra_msg),
                    },
                    _ => e,
                })?;
            }
        }
        Some("cml") => {
            parse_cml(&buffer)?;
        }
        _ => {
            return Err(Error::invalid_args(BAD_EXTENSION));
        }
    };
    Ok(())
}

/// Validates a JSON document according to the given schema.
pub fn validate_json(json: &Value, schema: &JsonSchema<'_>) -> Result<(), Error> {
    // Parse the schema
    let cmx_schema_json = serde_json::from_str(&schema.schema).map_err(|e| {
        Error::internal(format!("Couldn't read schema '{}' as JSON: {}", schema.name, e))
    })?;
    let mut scope = json_schema::Scope::new();
    let compiled_schema = scope.compile_and_return(cmx_schema_json, false).map_err(|e| {
        Error::internal(format!("Couldn't parse schema '{}': {:?}", schema.name, e))
    })?;

    // Validate the json
    let res = compiled_schema.validate(json);
    if !res.is_strictly_valid() {
        let mut err_msgs = Vec::new();
        for e in &res.errors {
            err_msgs.push(format!("{} at {}", e.get_title(), e.get_path()).into_boxed_str());
        }
        for u in &res.missing {
            err_msgs.push(
                format!("internal error: schema definition is missing URL {}", u).into_boxed_str(),
            );
        }
        // The ordering in which valico emits these errors is unstable.
        // Sort error messages so that the resulting message is predictable.
        err_msgs.sort_unstable();
        return Err(Error::validate_schema(&schema, err_msgs.join(", ")));
    }
    Ok(())
}

struct ValidationContext<'a> {
    document: &'a cml::Document,
    all_children: HashMap<&'a cml::Name, &'a cml::Child>,
    all_collections: HashSet<&'a cml::Name>,
    all_storage_and_sources: HashMap<&'a cml::Name, &'a cml::StorageFromRef>,
    all_runners: HashSet<&'a cml::Name>,
    all_resolvers: HashSet<&'a cml::Name>,
    all_environment_names: HashSet<&'a cml::Name>,
    all_event_names: HashSet<cml::Name>,
}

/// A name/identity of a capability exposed/offered to another component.
///
/// Exposed or offered capabilities have an identifier whose format
/// depends on the capability type. For directories and services this is
/// a path, while for storage this is a storage name. Paths and storage
/// names, however, are in different conceptual namespaces, and can't
/// collide with each other.
///
/// This enum allows such names to be specified disambuating what
/// namespace they are in.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Copy)]
enum CapabilityId<'a> {
    Service(&'a cml::Path),
    Protocol(&'a cml::Path),
    Directory(&'a cml::Path),
    Runner(&'a cml::Name),
    Resolver(&'a cml::Name),
    StorageType(&'a cml::StorageType),
    Event(&'a cml::Name),
    EventStream(&'a cml::Path),
}

impl<'a> CapabilityId<'a> {
    /// Return the string ID of this clause.
    pub fn as_str(&self) -> &'a str {
        match self {
            CapabilityId::Runner(n) | CapabilityId::Resolver(n) | CapabilityId::Event(n) => {
                n.as_str()
            }
            CapabilityId::Service(p)
            | CapabilityId::Protocol(p)
            | CapabilityId::Directory(p)
            | CapabilityId::EventStream(p) => p.as_str(),
            CapabilityId::StorageType(s) => s.as_str(),
        }
    }

    /// Human readable description of this capability type.
    pub fn type_str(&self) -> &'static str {
        match self {
            CapabilityId::Service(_) => "service",
            CapabilityId::Protocol(_) => "protocol",
            CapabilityId::Directory(_) => "directory",
            CapabilityId::Runner(_) => "runner",
            CapabilityId::Resolver(_) => "resolver",
            CapabilityId::StorageType(_) => "storage type",
            CapabilityId::Event(_) => "event",
            CapabilityId::EventStream(_) => "event_stream",
        }
    }

    /// Return the directory containing the capability.
    pub fn get_dir_path(&self) -> Option<&Path> {
        match self {
            CapabilityId::Directory(p) => Some(Path::new(p.as_str())),
            CapabilityId::Service(p) | CapabilityId::Protocol(p) => Path::new(p.as_str()).parent(),
            _ => None,
        }
    }

    /// Given a CapabilityClause (Use, Offer or Expose), return the set of target identifiers.
    ///
    /// When only one capability identifier is specified, the target identifier name is derived
    /// using the "as" clause. If an "as" clause is not specified, the target identifier is the
    /// same name as the source.
    ///
    /// When multiple capability identifiers are specified, the target names are the same as the
    /// source names.
    pub fn from_clause<'b, T>(clause: &'b T) -> Result<Vec<CapabilityId<'b>>, Error>
    where
        T: cml::CapabilityClause + cml::AsClause + cml::FilterClause + std::fmt::Debug,
    {
        // For directory/service/runner types, return the source name,
        // using the "as" clause to rename if neccessary.
        let alias = clause.r#as();
        if let Some(svc) = clause.service().as_ref() {
            return Ok(vec![CapabilityId::Service(cml::alias_or_path(alias, svc)?)]);
        } else if let Some(OneOrMany::One(protocol)) = clause.protocol().as_ref() {
            return Ok(vec![CapabilityId::Protocol(cml::alias_or_path(alias, protocol)?)]);
        } else if let Some(OneOrMany::Many(protocols)) = clause.protocol().as_ref() {
            return match (alias, protocols.len()) {
                (Some(valid_alias), 1) => {
                    Ok(vec![CapabilityId::Protocol(valid_alias.extract_path_borrowed()?)])
                }

                (Some(_), _) => Err(Error::validate(
                    "\"as\" field can only be specified when one `protocol` is supplied.",
                )),

                (None, _) => Ok(protocols
                    .iter()
                    .map(|svc: &cml::Path| CapabilityId::Protocol(svc))
                    .collect()),
            };
        } else if let Some(p) = clause.directory().as_ref() {
            return Ok(vec![CapabilityId::Directory(cml::alias_or_path(alias, p)?)]);
        } else if let Some(n) = clause.runner().as_ref() {
            return Ok(vec![CapabilityId::Runner(cml::alias_or_name(alias, n)?)]);
        } else if let Some(n) = clause.resolver().as_ref() {
            return Ok(vec![CapabilityId::Resolver(cml::alias_or_name(alias, n)?)]);
        } else if let Some(OneOrMany::One(n)) = clause.event().as_ref() {
            return Ok(vec![CapabilityId::Event(cml::alias_or_name(alias, n)?)]);
        } else if let Some(OneOrMany::Many(events)) = clause.event().as_ref() {
            return match (alias, clause.filter(), events.len()) {
                (Some(valid_alias), _, 1) => {
                    Ok(vec![CapabilityId::Event(valid_alias.extract_name_borrowed()?)])
                }
                (None, Some(_), 1) => Ok(vec![CapabilityId::Event(&events[0])]),
                (Some(_), None, _) => Err(Error::validate(
                    "\"as\" field can only be specified when one `event` is supplied",
                )),
                (None, Some(_), _) => Err(Error::validate(
                    "\"filter\" field can only be specified when one `event` is supplied",
                )),
                (Some(_), Some(_), _) => Err(Error::validate(
                    "\"as\",\"filter\" fields can only be specified when one `event` is supplied",
                )),
                (None, None, _) => {
                    Ok(events.iter().map(|event: &cml::Name| CapabilityId::Event(event)).collect())
                }
            };
        } else if let Some(_) = clause.event_stream().as_ref() {
            return Ok(vec![CapabilityId::EventStream(cml::alias_or_path(
                alias,
                &DEFAULT_EVENT_STREAM_PATH,
            )?)]);
        }

        // Offers rules prohibit using the "as" clause for storage; this is validated outside the
        // scope of this function.
        if let Some(p) = clause.storage().as_ref() {
            return Ok(vec![CapabilityId::StorageType(p)]);
        }

        // Unknown capability type.
        let supported_keywords = clause
            .supported()
            .into_iter()
            .map(|k| format!("\"{}\"", k))
            .collect::<Vec<_>>()
            .join(", ");
        Err(Error::validate(format!(
            "`{}` declaration is missing a capability keyword, one of: {}",
            clause.decl_type(),
            supported_keywords,
        )))
    }
}

impl<'a> ValidationContext<'a> {
    fn validate(&mut self) -> Result<(), Error> {
        // Ensure child components, collections, and storage don't use the
        // same name.
        //
        // We currently have the ability to distinguish between storage and
        // children/collections based on context, but still enforce name
        // uniqueness to give us flexibility in future.
        let all_children_names =
            self.document.all_children_names().into_iter().zip(iter::repeat("children"));
        let all_collection_names =
            self.document.all_collection_names().into_iter().zip(iter::repeat("collections"));
        let all_storage_names =
            self.document.all_storage_names().into_iter().zip(iter::repeat("storage"));
        let all_runner_names =
            self.document.all_runner_names().into_iter().zip(iter::repeat("runners"));
        let all_resolver_names =
            self.document.all_resolver_names().into_iter().zip(iter::repeat("resolvers"));
        let all_environment_names =
            self.document.all_environment_names().into_iter().zip(iter::repeat("environments"));
        ensure_no_duplicate_names(
            all_children_names
                .chain(all_collection_names)
                .chain(all_storage_names)
                .chain(all_runner_names)
                .chain(all_resolver_names)
                .chain(all_environment_names),
        )?;

        // Populate the sets of children and collections.
        if let Some(children) = &self.document.children {
            self.all_children = children.iter().map(|c| (&c.name, c)).collect();
        }
        self.all_collections = self.document.all_collection_names().into_iter().collect();
        self.all_storage_and_sources = self.document.all_storage_and_sources();
        self.all_runners = self.document.all_runner_names().into_iter().collect();
        self.all_resolvers = self.document.all_resolver_names().into_iter().collect();
        self.all_environment_names = self.document.all_environment_names().into_iter().collect();
        self.all_event_names = self.document.all_event_names()?.into_iter().collect();

        // Validate "children".
        let mut strong_dependencies = DirectedGraph::new();
        if let Some(children) = &self.document.children {
            for child in children {
                self.validate_child(&child, &mut strong_dependencies)?;
            }
        }

        // Validate "collections".
        if let Some(collections) = &self.document.collections {
            for collection in collections {
                self.validate_collection(&collection)?;
            }
        }

        // Validate "use".
        if let Some(uses) = self.document.r#use.as_ref() {
            let mut used_ids = HashMap::new();
            for use_ in uses.iter() {
                self.validate_use(&use_, &mut used_ids)?;
            }
        }

        // Validate "expose".
        if let Some(exposes) = self.document.expose.as_ref() {
            let mut used_ids = HashMap::new();
            for expose in exposes.iter() {
                self.validate_expose(&expose, &mut used_ids)?;
            }
        }

        // Validate "offer".
        if let Some(offers) = self.document.offer.as_ref() {
            let mut used_ids = HashMap::new();
            for offer in offers.iter() {
                self.validate_offer(&offer, &mut used_ids, &mut strong_dependencies)?;
            }
        }

        // Validate "storage".
        if let Some(storage) = self.document.storage.as_ref() {
            for s in storage.iter() {
                self.validate_component_ref("\"storage\" source", cml::AnyRef::from(&s.from))?;
            }
        }

        // Validate "runners".
        if let Some(runners) = self.document.runners.as_ref() {
            for r in runners.iter() {
                self.validate_component_ref("\"runner\" source", cml::AnyRef::from(&r.from))?;
            }
        }

        // Ensure we don't have a component with a "program" block which fails
        // to specify a runner.
        self.validate_runner_specified(
            self.document.program.as_ref(),
            self.document.r#use.as_ref(),
        )?;

        // Validate "environments".
        if let Some(environments) = &self.document.environments {
            for env in environments {
                self.validate_environment(&env, &mut strong_dependencies)?;
            }
        }

        // Check for dependency cycles
        match strong_dependencies.topological_sort() {
            Ok(_) => {}
            Err(e) => {
                return Err(Error::validate(format!(
                    "Strong dependency cycles were found. Break the cycle by removing a dependency or marking an offer as weak. Cycles: {}", e.format_cycle())));
            }
        }

        Ok(())
    }

    fn validate_child(
        &self,
        child: &'a cml::Child,
        strong_dependencies: &mut DirectedGraph<DependencyNode<'a>>,
    ) -> Result<(), Error> {
        if let Some(environment_ref) = &child.environment {
            match environment_ref {
                cml::EnvironmentRef::Named(environment_name) => {
                    if !self.all_environment_names.contains(&environment_name) {
                        return Err(Error::validate(format!(
                            "\"{}\" does not appear in \"environments\"",
                            &environment_name
                        )));
                    }
                    let source = DependencyNode::Environment(environment_name.as_str());
                    let target = DependencyNode::Child(child.name.as_str());
                    strong_dependencies.add_edge(source, target);
                }
            }
        }
        Ok(())
    }

    fn validate_collection(&self, collection: &'a cml::Collection) -> Result<(), Error> {
        if let Some(environment_ref) = &collection.environment {
            match environment_ref {
                cml::EnvironmentRef::Named(environment_name) => {
                    if !self.all_environment_names.contains(&environment_name) {
                        return Err(Error::validate(format!(
                            "\"{}\" does not appear in \"environments\"",
                            &environment_name
                        )));
                    }
                    // If there is an environment, we don't need to account for it in the dependency
                    // graph because a collection is always a sink node.
                }
            }
        }
        Ok(())
    }

    fn validate_use(
        &self,
        use_: &'a cml::Use,
        used_ids: &mut HashMap<&'a str, CapabilityId<'a>>,
    ) -> Result<(), Error> {
        match (&use_.runner, &use_.r#as) {
            (Some(_), Some(_)) => {
                Err(Error::validate("\"as\" field cannot be used with \"runner\""))
            }
            _ => Ok(()),
        }?;

        match (&use_.event, &use_.from) {
            (Some(_), None) => Err(Error::validate("\"from\" should be present with \"event\"")),
            _ => Ok(()),
        }?;

        match (&use_.event, &use_.filter) {
            (None, Some(_)) => Err(Error::validate("\"filter\" can only be used with \"event\"")),
            _ => Ok(()),
        }?;

        let storage = use_.storage.as_ref().map(|s| s.as_str());
        match (storage, &use_.r#as) {
            (Some("meta"), Some(_)) => {
                Err(Error::validate("\"as\" field cannot be used with storage type \"meta\""))
            }
            _ => Ok(()),
        }?;
        match (storage, &use_.from) {
            (Some(_), Some(_)) => {
                Err(Error::validate("\"from\" field cannot be used with \"storage\""))
            }
            _ => Ok(()),
        }?;

        if let Some(event_stream) = use_.event_stream.as_ref() {
            let events = event_stream.to_vec();
            for event in events {
                if !self.all_event_names.contains(event) {
                    return Err(Error::validate(format!(
                        "Event \"{}\" in event stream not found in any \"use\" declaration.",
                        event
                    )));
                }
            }
        }

        // Disallow multiple capability ids of the same name.
        let capability_ids = CapabilityId::from_clause(use_)?;
        for capability_id in capability_ids {
            if used_ids.insert(capability_id.as_str(), capability_id).is_some() {
                return Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"use\" target {}",
                    capability_id.as_str(),
                    capability_id.type_str()
                )));
            }
            let dir = match capability_id.get_dir_path() {
                Some(d) => d,
                None => continue,
            };

            // Validate that paths-based capabilities (service, directory, protocol)
            // are not prefixes of each other.
            for (_, used_id) in used_ids.iter() {
                if capability_id == *used_id {
                    continue;
                }
                let used_dir = match used_id.get_dir_path() {
                    Some(d) => d,
                    None => continue,
                };

                if match (used_id, capability_id) {
                    // Directories can't be the same or partially overlap.
                    (CapabilityId::Directory(_), CapabilityId::Directory(_)) => {
                        dir == used_dir || dir.starts_with(used_dir) || used_dir.starts_with(dir)
                    }

                    // Protocols and Services can't overlap with Directories.
                    (_, CapabilityId::Directory(_)) | (CapabilityId::Directory(_), _) => {
                        dir == used_dir || dir.starts_with(used_dir) || used_dir.starts_with(dir)
                    }

                    // Protocols and Services containing directories may be same, but
                    // partial overlap is disallowed.
                    (_, _) => {
                        dir != used_dir && (dir.starts_with(used_dir) || used_dir.starts_with(dir))
                    }
                } {
                    return Err(Error::validate(format!(
                        "{} \"{}\" is a prefix of \"use\" target {} \"{}\"",
                        capability_id.type_str(),
                        capability_id.as_str(),
                        used_id.type_str(),
                        used_id.as_str()
                    )));
                }
            }
        }

        // All directory "use" expressions must have directory rights.
        if use_.directory.is_some() {
            match &use_.rights {
                Some(rights) => self.validate_directory_rights(&rights)?,
                None => return Err(Error::validate("Rights required for this use statement.")),
            };
        }

        Ok(())
    }

    fn validate_expose(
        &self,
        expose: &'a cml::Expose,
        used_ids: &mut HashMap<&'a str, CapabilityId<'a>>,
    ) -> Result<(), Error> {
        self.validate_from_clause("expose", expose)?;

        // Ensure that if the expose target is framework, the source target is self always.
        if expose.to == Some(cml::ExposeToRef::Framework) {
            match &expose.from {
                OneOrMany::One(cml::ExposeFromRef::Self_) => {}
                OneOrMany::Many(vec)
                    if vec.iter().all(|from| *from == cml::ExposeFromRef::Self_) => {}
                _ => {
                    return Err(Error::validate("Expose to framework can only be done from self."))
                }
            }
        }

        // Ensure directory rights are specified if exposing from self.
        if expose.directory.is_some() {
            if expose.from.iter().any(|r| *r == cml::ExposeFromRef::Self_)
                || expose.rights.is_some()
            {
                match &expose.rights {
                    Some(rights) => self.validate_directory_rights(&rights)?,
                    None => return Err(Error::validate(
                        "Rights required for this expose statement as it is exposing from self.",
                    )),
                };
            }

            // Exposing a subdirectory makes sense for routing but when exposing to framework,
            // the subdir should be exposed directly.
            if expose.to == Some(cml::ExposeToRef::Framework) {
                if expose.subdir.is_some() {
                    return Err(Error::validate(
                        "`subdir` is not supported for expose to framework. Directly expose the subdirectory instead."
                    ));
                }
            }
        }

        // Ensure that resolvers exposed from self are defined in `resolvers`.
        if let Some(resolver_name) = &expose.resolver {
            // Resolvers can only have a single `from` clause.
            if expose.from.iter().any(|r| *r == cml::ExposeFromRef::Self_) {
                if !self.all_resolvers.contains(resolver_name) {
                    return Err(Error::validate(format!(
                       "Resolver \"{}\" is exposed from self, so it must be declared in \"resolvers\"", resolver_name
                   )));
                }
            }
        }

        // Ensure we haven't already exposed an entity of the same name.
        let capability_ids = CapabilityId::from_clause(expose)?;
        for capability_id in capability_ids {
            if used_ids.insert(capability_id.as_str(), capability_id).is_some() {
                return Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"expose\" target {} for \"{}\"",
                    capability_id.as_str(),
                    capability_id.type_str(),
                    expose.to.as_ref().unwrap_or(&cml::ExposeToRef::Realm)
                )));
            }
        }

        Ok(())
    }

    fn validate_offer(
        &self,
        offer: &'a cml::Offer,
        used_ids: &mut HashMap<&'a cml::Name, HashMap<&'a str, CapabilityId<'a>>>,
        strong_dependencies: &mut DirectedGraph<DependencyNode<'a>>,
    ) -> Result<(), Error> {
        self.validate_from_clause("offer", offer)?;

        // Ensure directory rights are specified if offering from self.
        if offer.directory.is_some() {
            // Directories can only have a single `from` clause.
            if offer.from.iter().any(|r| *r == cml::OfferFromRef::Self_) || offer.rights.is_some() {
                match &offer.rights {
                    Some(rights) => self.validate_directory_rights(&rights)?,
                    None => {
                        return Err(Error::validate(
                            "Rights required for this offer as it is offering from self.",
                        ))
                    }
                };
            }
        }

        // Ensure that resolvers offered from self are defined in `resolvers`.
        if let Some(resolver_name) = &offer.resolver {
            // Resolvers can only have a single `from` clause.
            if offer.from.iter().any(|r| *r == cml::OfferFromRef::Self_) {
                if !self.all_resolvers.contains(resolver_name) {
                    return Err(Error::validate(format!(
                        "Resolver \"{}\" is offered from self, so it must be declared in \
                       \"resolvers\"",
                        resolver_name
                    )));
                }
            }
        }

        // Ensure that dependency can only be provided for directories and protocols
        if offer.dependency.is_some() && offer.directory.is_none() && offer.protocol.is_none() {
            return Err(Error::validate(
                "Dependency can only be provided for protocol and directory capabilities",
            ));
        }

        // Ensure that only events can have filter.
        match (&offer.event, &offer.filter) {
            (None, Some(_)) => Err(Error::validate("\"filter\" can only be used with \"event\"")),
            _ => Ok(()),
        }?;

        // Validate every target of this offer.
        for to in &offer.to.0 {
            // Ensure the "to" value is a child.
            let to_target = match to {
                cml::OfferToRef::Named(ref name) => name,
            };

            // Check that any referenced child actually exists.
            if !self.all_children.contains_key(to_target)
                && !self.all_collections.contains(to_target)
            {
                return Err(Error::validate(format!(
                    "\"{}\" is an \"offer\" target but it does not appear in \"children\" \
                     or \"collections\"",
                    to
                )));
            }

            // Storage cannot be aliased when offered. Return an error if it is used.
            if offer.storage.is_some() && offer.r#as.is_some() {
                return Err(Error::validate(
                    "\"as\" field cannot be used for storage offer targets",
                ));
            }

            // Ensure that a target is not offered more than once.
            let target_cap_ids = CapabilityId::from_clause(offer)?;
            let ids_for_entity = used_ids.entry(to_target).or_insert(HashMap::new());
            for target_cap_id in target_cap_ids {
                if ids_for_entity.insert(target_cap_id.as_str(), target_cap_id).is_some() {
                    return Err(Error::validate(format!(
                        "\"{}\" is a duplicate \"offer\" target {} for \"{}\"",
                        target_cap_id.as_str(),
                        target_cap_id.type_str(),
                        to
                    )));
                }
            }

            // Ensure we are not offering a capability back to its source.
            if offer.storage.is_some() {
                // Storage can only have a single `from` clause and this has been
                // verified.
                if let OneOrMany::One(cml::OfferFromRef::Named(name)) = &offer.from {
                    if let Some(cml::StorageFromRef::Named(source)) =
                        self.all_storage_and_sources.get(&name)
                    {
                        if to_target == source {
                            return Err(Error::validate(format!(
                                "Storage offer target \"{}\" is same as source",
                                to
                            )));
                        }
                    }
                }
            } else {
                for reference in offer.from.to_vec() {
                    match reference {
                        cml::OfferFromRef::Named(name) if name == to_target => {
                            return Err(Error::validate(format!(
                                "Offer target \"{}\" is same as source",
                                to
                            )));
                        }
                        _ => {}
                    }
                }
            }

            // Collect strong dependencies. We'll check for dependency cycles after all offer
            // declarations are validated.
            for from in offer.from.to_vec().iter() {
                let is_strong = if offer.directory.is_some() || offer.protocol.is_some() {
                    offer.dependency.as_ref().unwrap_or(&cml::DependencyType::Strong)
                        == &cml::DependencyType::Strong
                } else {
                    true
                };
                if is_strong {
                    if let cml::OfferFromRef::Named(from) = from {
                        match to {
                            cml::OfferToRef::Named(to) => {
                                let source = DependencyNode::Child(from.as_str());
                                let target = DependencyNode::Child(to.as_str());
                                strong_dependencies.add_edge(source, target);
                            }
                        }
                    }
                }
            }
        }

        Ok(())
    }

    /// Validates that the from clause:
    ///
    /// - is applicable to the capability type,
    /// - does not contain duplicates,
    /// - references names that exist.
    ///
    /// `verb` is used in any error messages and is expected to be "offer", "expose", etc.
    fn validate_from_clause<T>(&self, verb: &str, cap: &T) -> Result<(), Error>
    where
        T: cml::CapabilityClause + cml::FromClause,
    {
        let from = cap.from_();
        if cap.service().is_none() && from.is_many() {
            return Err(Error::validate(format!(
                "\"{}\" capabilities cannot have multiple \"from\" clauses",
                cap.capability_name()
            )));
        }

        if from.is_many() {
            ensure_no_duplicate_values(&cap.from_())?;
        }

        // If offered cap is a storage type, then "from" should be interpreted
        // as a storage name. Otherwise, it should be interpreted as a child
        // or collection.
        let reference_description = format!("\"{}\" source", verb);
        if cap.storage().is_some() {
            for from_clause in from {
                self.validate_storage_ref(&reference_description, from_clause)?;
            }
        } else {
            for from_clause in from {
                self.validate_component_ref(&reference_description, from_clause)?;
            }
        }
        Ok(())
    }

    /// Validates that the given component exists.
    ///
    /// - `reference_description` is a human-readable description of
    ///   the reference used in error message, such as `"offer" source`.
    /// - `component_ref` is a reference to a component. If the reference
    ///   is a named child, we ensure that the child component exists.
    fn validate_component_ref(
        &self,
        reference_description: &str,
        component_ref: cml::AnyRef,
    ) -> Result<(), Error> {
        match component_ref {
            cml::AnyRef::Named(name) => {
                // Ensure we have a child defined by that name.
                if !self.all_children.contains_key(name) {
                    return Err(Error::validate(format!(
                        "{} \"{}\" does not appear in \"children\"",
                        reference_description, component_ref
                    )));
                }
                Ok(())
            }
            // We don't attempt to validate other reference types.
            _ => Ok(()),
        }
    }

    /// Validates that the given storage reference exists.
    ///
    /// - `reference_description` is a human-readable description of
    ///   the reference used in error message, such as `"storage" source`.
    /// - `storage_ref` is a reference to a storage source.
    fn validate_storage_ref(
        &self,
        reference_description: &str,
        storage_ref: cml::AnyRef,
    ) -> Result<(), Error> {
        if let cml::AnyRef::Named(name) = storage_ref {
            if !self.all_storage_and_sources.contains_key(name) {
                return Err(Error::validate(format!(
                    "{} \"{}\" does not appear in \"storage\"",
                    reference_description, storage_ref,
                )));
            }
        }

        Ok(())
    }

    /// Validates that directory rights for all route types are valid, i.e that it does not
    /// contain duplicate rights.
    fn validate_directory_rights(&self, rights_clause: &cml::Rights) -> Result<(), Error> {
        let mut rights = HashSet::new();
        for right_token in rights_clause.0.iter() {
            for right in right_token.expand() {
                if !rights.insert(right) {
                    return Err(Error::validate(format!(
                        "\"{}\" is duplicated in the rights clause.",
                        right_token
                    )));
                }
            }
        }
        Ok(())
    }

    /// Ensure we don't have a component with a "program" block which fails
    /// to specify a runner.
    fn validate_runner_specified(
        &self,
        program: Option<&serde_json::map::Map<String, serde_json::value::Value>>,
        use_: Option<&Vec<cml::Use>>,
    ) -> Result<(), Error> {
        // Components that have no "program" don't need a runner.
        if program.is_none() {
            return Ok(());
        }

        // Otherwise, ensure a runner is being used.
        let mut found_runner = false;
        if let Some(use_) = use_ {
            found_runner = use_.iter().any(|u| u.runner.is_some())
        }
        if !found_runner {
            return Err(Error::validate(concat!(
                "Component has a 'program' block defined, but doesn't 'use' ",
                "a runner capability. Components need to 'use' a runner ",
                "to actually execute code."
            )));
        }

        Ok(())
    }

    fn validate_environment(
        &self,
        environment: &'a cml::Environment,
        strong_dependencies: &mut DirectedGraph<DependencyNode<'a>>,
    ) -> Result<(), Error> {
        match &environment.extends {
            Some(cml::EnvironmentExtends::None) => {
                if environment.stop_timeout_ms.is_none() {
                    return Err(Error::validate(
                        "'__stop_timeout_ms' must be provided if the environment does not extend \
                        another environment",
                    ));
                }
            }
            Some(cml::EnvironmentExtends::Realm) | None => {}
        }

        if let Some(runners) = &environment.runners {
            let mut used_names = HashMap::new();
            for registration in runners {
                // Validate that this name is not already used.
                let name = registration.r#as.as_ref().unwrap_or(&registration.runner);
                if let Some(previous_runner) = used_names.insert(name, &registration.runner) {
                    return Err(Error::validate(format!(
                        "Duplicate runners registered under name \"{}\": \"{}\" and \"{}\".",
                        name, &registration.runner, previous_runner
                    )));
                }

                // Ensure that the environment is defined in `runners` if it comes from `self`.
                if registration.from == cml::RegistrationRef::Self_
                    && !self.all_runners.contains(&registration.runner)
                {
                    return Err(Error::validate(format!(
                        "Runner \"{}\" registered in environment is not in \"runners\"",
                        &registration.runner,
                    )));
                }

                self.validate_component_ref(
                    &format!("\"{}\" runner source", &registration.runner),
                    cml::AnyRef::from(&registration.from),
                )?;

                // Ensure there are no cycles, such as a resolver in an environment being assigned
                // to a child which the resolver depends on.
                if let cml::RegistrationRef::Named(child_name) = &registration.from {
                    let source = DependencyNode::Child(child_name.as_str());
                    let target = DependencyNode::Environment(environment.name.as_str());
                    strong_dependencies.add_edge(source, target);
                }
            }
        }

        if let Some(resolvers) = &environment.resolvers {
            let mut used_schemes = HashMap::new();
            for registration in resolvers {
                // Validate that the scheme is not already used.
                if let Some(previous_resolver) =
                    used_schemes.insert(&registration.scheme, &registration.resolver)
                {
                    return Err(Error::validate(format!(
                        "scheme \"{}\" for resolver \"{}\" is already registered; \
                        previously registered to resolver \"{}\".",
                        &registration.scheme, &registration.resolver, previous_resolver
                    )));
                }

                self.validate_component_ref(
                    &format!("\"{}\" resolver source", &registration.resolver),
                    cml::AnyRef::from(&registration.from),
                )?;
                // Ensure there are no cycles, such as a resolver in an environment being assigned
                // to a child which the resolver depends on.
                if let cml::RegistrationRef::Named(child_name) = &registration.from {
                    let source = DependencyNode::Child(child_name.as_str());
                    let target = DependencyNode::Environment(environment.name.as_str());
                    strong_dependencies.add_edge(source, target);
                }
            }
        }
        Ok(())
    }
}

/// Given an iterator with `(key, name)` tuples, ensure that `key` doesn't
/// appear twice. `name` is used in generated error messages.
fn ensure_no_duplicate_names<'a, I>(values: I) -> Result<(), Error>
where
    I: Iterator<Item = (&'a cml::Name, &'a str)>,
{
    let mut seen_keys = HashMap::new();
    for (key, name) in values {
        if let Some(preexisting_name) = seen_keys.insert(key, name) {
            return Err(Error::validate(format!(
                "identifier \"{}\" is defined twice, once in \"{}\" and once in \"{}\"",
                key, name, preexisting_name
            )));
        }
    }
    Ok(())
}

/// Returns an error if the iterator contains duplicate values.
fn ensure_no_duplicate_values<'a, I, V>(values: I) -> Result<(), Error>
where
    I: IntoIterator<Item = &'a V>,
    V: 'a + Hash + Eq + Display,
{
    let mut seen = HashSet::new();
    for value in values {
        if !seen.insert(value) {
            return Err(Error::validate(format!("Found duplicate value \"{}\" in array.", value)));
        }
    }
    Ok(())
}

/// A node in the DependencyGraph. This enum is used to differentiate between node types.
#[derive(Copy, Clone, Hash, Ord, Debug, PartialOrd, PartialEq, Eq)]
enum DependencyNode<'a> {
    Child(&'a str),
    Environment(&'a str),
}

impl<'a> fmt::Display for DependencyNode<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DependencyNode::Child(name) => write!(f, "child {}", name),
            DependencyNode::Environment(name) => write!(f, "environment {}", name),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use lazy_static::lazy_static;
    use matches::assert_matches;
    use serde_json::json;
    use std::io::Write;
    use tempfile::TempDir;

    macro_rules! test_validate_cml {
        (
            $(
                $test_name:ident($input:expr, $($pattern:tt)+),
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    let input = format!("{}", $input);
                    let result = write_and_validate("test.cml", input.as_bytes());
                    assert_matches!(result, $($pattern)+);
                }
            )+
        }
    }

    macro_rules! test_validate_cmx {
        (
            $(
                $test_name:ident($input:expr, $($pattern:tt)+),
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    let input = format!("{}", $input);
                    let result = write_and_validate("test.cmx", input.as_bytes());
                    assert_matches!(result, $($pattern)+);
                }
            )+
        }
    }

    fn write_and_validate(filename: &str, input: &[u8]) -> Result<(), Error> {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_file_path = tmp_dir.path().join(filename);
        File::create(&tmp_file_path).unwrap().write_all(input).unwrap();
        validate(&vec![tmp_file_path], &[])
    }

    #[test]
    fn test_validate_invalid_json_fails() {
        let result = write_and_validate("test.cml", b"{");
        let expected_err = r#" --> 1:2
  |
1 | {
  |  ^---
  |
  = expected identifier or string"#;
        assert_matches!(result, Err(Error::Parse { err, .. }) if &err == expected_err);
    }

    #[test]
    fn test_cml_json5() {
        let input = r##"{
            "expose": [
                // Here are some services to expose.
                { "service": "/loggers/fuchsia.logger.Log", "from": "#logger", },
                { "directory": "/volumes/blobfs", "from": "self", "rights": ["rw*"]},
            ],
            "children": [
                {
                    'name': 'logger',
                    'url': 'fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm',
                },
            ],
        }"##;
        let result = write_and_validate("test.cml", input.as_bytes());
        assert_matches!(result, Ok(()));
    }

    test_validate_cml! {
        // program
        test_cml_empty_json(
            json!({}),
            Ok(())
        ),
        test_cml_program(
            json!(
                {
                    "program": { "binary": "bin/app" },
                    "use": [ { "runner": "elf" } ],
                }
            ),
            Ok(())
        ),

        // use
        test_cml_use(
            json!({
                "use": [
                  { "service": "/fonts/CoolFonts", "as": "/svc/fuchsia.fonts.Provider" },
                  { "service": "/svc/fuchsia.sys2.Realm", "from": "framework" },
                  { "protocol": "/fonts/CoolFonts", "as": "/svc/MyFonts" },
                  { "protocol": "/svc/fuchsia.test.hub.HubReport", "from": "framework" },
                  { "protocol": ["/svc/fuchsia.ui.scenic.Scenic", "/svc/fuchsia.net.Connectivity"] },
                  {
                    "directory": "/data/assets",
                    "rights": ["rw*"],
                  },
                  {
                    "directory": "/data/config",
                    "from": "realm",
                    "rights": ["rx*"],
                    "subdir": "fonts/all",
                  },
                  { "storage": "data", "as": "/example" },
                  { "storage": "cache", "as": "/tmp" },
                  { "storage": "meta" },
                  { "runner": "elf" },
                  { "event": [ "started", "stopped"], "from": "realm" },
                  { "event": [ "launched"], "from": "framework" },
                  { "event": "destroyed", "from": "framework", "as": "destroyed_x" },
                  {
                    "event": "capability_ready_diagnostics",
                    "as": "capability_ready",
                    "from": "realm",
                    "filter": {
                        "path": "/diagnositcs"
                    }
                  },
                  {
                    "event_stream": [ "started", "stopped", "launched" ]
                  },
                  {
                    "event_stream": [ "started", "stopped", "launched" ],
                    "as": "/svc/my_stream"
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_use_event_missing_from(
            json!({
                "use": [
                    { "event": "started" },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"from\" should be present with \"event\""
        ),
        test_cml_use_missing_props(
            json!({
                "use": [ { "as": "/svc/fuchsia.logger.Log" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "`use` declaration is missing a capability keyword, one of: \"service\", \"protocol\", \"directory\", \"storage\", \"runner\", \"event\", \"event_stream\""
        ),
        test_cml_use_as_with_meta_storage(
            json!({
                "use": [ { "storage": "meta", "as": "/meta" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field cannot be used with storage type \"meta\""
        ),
        test_cml_use_as_with_runner(
            json!({
                "use": [ { "runner": "elf", "as": "xxx" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field cannot be used with \"runner\""
        ),
        test_cml_use_from_with_meta_storage(
            json!({
                "use": [ { "storage": "cache", "from": "realm" } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"from\" field cannot be used with \"storage\""
        ),
        test_cml_use_invalid_from(
            json!({
                "use": [
                  { "service": "/fonts/CoolFonts", "from": "self" }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"self\", expected \"realm\", \"framework\", or none"
        ),
        test_cml_use_bad_as(
            json!({
                "use": [
                    {
                        "protocol": ["/fonts/CoolFonts", "/fonts/FunkyFonts"],
                        "as": "/fonts/MyFonts"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field can only be specified when one `protocol` is supplied."
        ),
        test_cml_use_bad_duplicate_targets(
            json!({
                "use": [
                  { "service": "/svc/fuchsia.sys2.Realm", "from": "framework" },
                  { "protocol": "/svc/fuchsia.sys2.Realm", "from": "framework" },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"/svc/fuchsia.sys2.Realm\" is a duplicate \"use\" target protocol"
        ),
        test_cml_use_bad_duplicate_protocol(
            json!({
                "use": [
                  { "protocol": ["/svc/fuchsia.sys2.Realm", "/svc/fuchsia.sys2.Realm"] },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: array with duplicate element, expected a path or nonempty array of paths, with unique elements"
        ),
        test_cml_use_empty_protocols(
            json!({
                "use": [
                    {
                        "protocol": [],
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a path or nonempty array of paths, with unique elements"
        ),
        test_cml_use_bad_subdir(
            json!({
                "use": [
                  {
                    "directory": "/data/config",
                    "from": "realm",
                    "rights": [ "r*" ],
                    "subdir": "/",
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_use_resolver_fails(
            json!({
                "use": [
                    {
                        "resolver": "pkg_resolver",
                        "from": "realm",
                    },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "`use` declaration is missing a capability keyword, one of: \"service\", \"protocol\", \"directory\", \"storage\", \"runner\", \"event\", \"event_stream\""
        ),

        test_cml_use_disallows_nested_dirs(
            json!({
                "use": [
                    { "directory": "/foo/bar", "rights": [ "r*" ] },
                    { "directory": "/foo/bar/baz", "rights": [ "r*" ] },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "directory \"/foo/bar/baz\" is a prefix of \"use\" target directory \"/foo/bar\""
        ),
        test_cml_use_disallows_common_prefixes_protocol(
            json!({
                "use": [
                    { "directory": "/foo/bar", "rights": [ "r*" ] },
                    { "protocol": "/foo/bar/fuchsia.2" },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "protocol \"/foo/bar/fuchsia.2\" is a prefix of \"use\" target directory \"/foo/bar\""
        ),
        test_cml_use_disallows_common_prefixes_service(
            json!({
                "use": [
                    { "directory": "/foo/bar", "rights": [ "r*" ] },
                    { "service": "/foo/bar/baz/fuchsia.logger.Log" },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "service \"/foo/bar/baz/fuchsia.logger.Log\" is a prefix of \"use\" target directory \"/foo/bar\""
        ),
        test_cml_use_disallows_filter_on_non_events(
            json!({
                "use": [
                    { "directory": "/foo/bar", "rights": [ "r*" ], "filter": {"path": "/diagnostics"} },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"filter\" can only be used with \"event\""
        ),
        test_cml_use_bad_as_in_event(
            json!({
                "use": [
                    {
                        "event": ["destroyed", "stopped"],
                        "from": "realm",
                        "as": "gone"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field can only be specified when one `event` is supplied"
        ),
        test_cml_use_duplicate_events(
            json!({
                "use": [
                    {
                        "event": ["destroyed", "started"],
                        "from": "realm",
                    },
                    {
                        "event": ["destroyed"],
                        "from": "realm",
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"destroyed\" is a duplicate \"use\" target event"
        ),
        test_cml_use_event_stream_missing_events(
            json!({
                "use": [
                    {
                        "event_stream": ["destroyed"],
                    },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Event \"destroyed\" in event stream not found in any \"use\" declaration."
        ),
        test_cml_use_bad_filter_in_event(
            json!({
                "use": [
                    {
                        "event": ["destroyed", "stopped"],
                        "filter": {"path": "/diagnostics"},
                        "from": "realm"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"filter\" field can only be specified when one `event` is supplied"
        ),
        test_cml_use_bad_filter_and_as_in_event(
            json!({
                "use": [
                    {
                        "event": ["destroyed", "stopped"],
                        "from": "framework",
                        "as": "gone",
                        "filter": {"path": "/diagnostics"}
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\",\"filter\" fields can only be specified when one `event` is supplied"
        ),
        // expose
        test_cml_expose(
            json!({
                "expose": [
                    {
                        "service": "/loggers/fuchsia.logger.Log",
                        "from": "#logger",
                        "as": "/svc/logger"
                    },
                    {
                        "protocol": "/svc/A",
                        "from": "self",
                    },
                    {
                        "protocol": ["/svc/B", "/svc/C"],
                        "from": "self",
                    },
                    {
                        "directory": "/volumes/blobfs",
                        "from": "self",
                        "rights": ["r*"],
                        "subdir": "blob",
                    },
                    { "directory": "/hub", "from": "framework" },
                    { "runner": "elf", "from": "#logger",  },
                    { "resolver": "pkg_resolver", "from": "#logger" },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_expose_service_multiple_from(
            json!({
                "expose": [
                    {
                        "service": "/loggers/fuchsia.logger.Log",
                        "from": [ "#logger", "self" ],
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_expose_all_valid_chars(
            json!({
                "expose": [
                    { "service": "/loggers/fuchsia.logger.Log", "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-." }
                ],
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-.",
                        "url": "https://www.google.com/gmail"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_expose_missing_props(
            json!({
                "expose": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `from`"
        ),
        test_cml_expose_missing_from(
            json!({
                "expose": [
                    { "service": "/loggers/fuchsia.logger.Log", "from": "#missing" }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"expose\" source \"#missing\" does not appear in \"children\""
        ),
        test_cml_expose_duplicate_target_paths(
            json!({
                "expose": [
                    { "service": "/fonts/CoolFonts", "from": "self" },
                    { "service": "/svc/logger", "from": "#logger", "as": "/thing" },
                    { "directory": "/thing", "from": "self" , "rights": ["rx*"] }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"/thing\" is a duplicate \"expose\" target directory for \"realm\""
        ),
        test_cml_expose_invalid_multiple_from(
            json!({
                    "expose": [ {
                        "protocol": "/svc/fuchsua.logger.Log",
                        "from": [ "self", "#logger" ],
                    } ],
                    "children": [
                        {
                            "name": "logger",
                            "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                        },
                    ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"protocol\" capabilities cannot have multiple \"from\" clauses"
        ),
        test_cml_expose_bad_from(
            json!({
                "expose": [ {
                    "service": "/loggers/fuchsia.logger.Log", "from": "realm"
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"realm\", expected one or an array of \"framework\", \"self\", or \"#<child-name>\""
        ),
        // if "as" is specified, only 1 "protocol" array item is allowed.
        test_cml_expose_bad_as(
            json!({
                "expose": [
                    {
                        "protocol": ["/svc/A", "/svc/B"],
                        "from": "self",
                        "as": "/thing"
                    },
                ],
                "children": [
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field can only be specified when one `protocol` is supplied."
        ),
        test_cml_expose_bad_duplicate_targets(
            json!({
                "expose": [
                    {
                        "protocol": ["/svc/A", "/svc/B"],
                        "from": "self"
                    },
                    {
                        "protocol": "/svc/A",
                        "from": "self"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"/svc/A\" is a duplicate \"expose\" target protocol for \"realm\""
        ),
        test_cml_expose_empty_protocols(
            json!({
                "expose": [
                    {
                        "protocol": [],
                        "from": "self",
                        "as": "/thing"
                    }
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a path or nonempty array of paths, with unique elements"
        ),
        test_cml_expose_bad_subdir(
            json!({
                "expose": [
                    {
                        "directory": "/volumes/blobfs",
                        "from": "self",
                        "rights": ["r*"],
                        "subdir": "/",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_expose_invalid_subdir_to_framework(
            json!({
                "expose": [
                    {
                        "directory": "/volumes/blobfs",
                        "from": "self",
                        "to": "framework",
                        "rights": ["r*"],
                        "subdir": "blob",
                    },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "`subdir` is not supported for expose to framework. Directly expose the subdirectory instead."),
        test_cml_expose_resolver_from_self(
            json!({
                "expose": [
                    {
                        "resolver": "pkg_resolver",
                        "from": "self",
                    },
                ],
                "resolvers": [
                    {
                        "name": "pkg_resolver",
                        "path": "/svc/fuchsia.sys2.ComponentResolver",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_expose_resolver_from_self_missing(
            json!({
                "expose": [
                    {
                        "resolver": "pkg_resolver",
                        "from": "self",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Resolver \"pkg_resolver\" is exposed from self, so it must be declared in \"resolvers\""
        ),
        test_cml_expose_to_framework_ok(
            json!({
                "expose": [
                    {
                        "directory": "/foo",
                        "from": "self",
                        "rights": ["r*"],
                        "to": "framework"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_expose_to_framework_invalid(
            json!({
                "expose": [
                    {
                        "directory": "/foo",
                        "from": "#logger",
                        "to": "framework"
                    }
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Expose to framework can only be done from self."
        ),

        // offer
        test_cml_offer(
            json!({
                "offer": [
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#logger",
                        "to": [ "#echo_server", "#modular" ],
                        "as": "/svc/fuchsia.logger.SysLog"
                    },
                    {
                        "service": "/svc/fuchsia.fonts.Provider",
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "protocol": "/svc/fuchsia.fonts.LegacyProvider",
                        "from": "realm",
                        "to": [ "#echo_server" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "protocol": [
                            "/svc/fuchsia.settings.Accessibility",
                            "/svc/fuchsia.ui.scenic.Scenic"
                        ],
                        "from": "realm",
                        "to": [ "#echo_server" ],
                        "dependency": "strong"
                    },
                    {
                        "directory": "/data/assets",
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "rights": ["rw*"]
                    },
                    {
                        "directory": "/data/index",
                        "subdir": "files",
                        "from": "realm",
                        "to": [ "#modular" ],
                        "dependency": "weak_for_migration"
                    },
                    {
                        "directory": "/hub",
                        "from": "framework",
                        "to": [ "#modular" ],
                        "as": "/hub",
                        "dependency": "strong"
                    },
                    {
                        "storage": "data",
                        "from": "#minfs",
                        "to": [ "#modular", "#logger" ]
                    },
                    {
                        "runner": "elf",
                        "from": "realm",
                        "to": [ "#modular", "#logger" ]
                    },
                    {
                        "resolver": "pkg_resolver",
                        "from": "realm",
                        "to": [ "#modular" ],
                    },
                    {
                        "event": "capability_ready",
                        "from": "realm",
                        "to": [ "#modular" ],
                        "as": "capability-ready-for-modular",
                        "filter": {
                            "path": "/modular"
                        }
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    },
                ],
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent",
                    },
                ],
                "storage": [
                    {
                        "name": "minfs",
                        "from": "realm",
                        "path": "/minfs",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_offer_service_multiple_from(
            json!({
                "offer": [
                    {
                        "service": "/loggers/fuchsia.logger.Log",
                        "from": [ "#logger", "self" ],
                        "to": [ "#echo_server" ],
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_offer_all_valid_chars(
            json!({
                "offer": [
                    {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "to": [ "#abcdefghijklmnopqrstuvwxyz0123456789_-to" ],
                    },
                    {
                        "storage": "data",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-storage",
                        "to": [ "#abcdefghijklmnopqrstuvwxyz0123456789_-to" ],
                    }
                ],
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "url": "https://www.google.com/gmail"
                    },
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-to",
                        "url": "https://www.google.com/gmail"
                    },
                ],
                "storage": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-storage",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "path": "/example"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_offer_missing_props(
            json!({
                "offer": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `from`"
        ),
        test_cml_offer_missing_from(
            json!({
                    "offer": [ {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#missing",
                        "to": [ "#echo_server" ],
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"offer\" source \"#missing\" does not appear in \"children\""
        ),
        test_cml_storage_offer_missing_from(
            json!({
                    "offer": [ {
                        "storage": "cache",
                        "from": "#missing",
                        "to": [ "#echo_server" ],
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"offer\" source \"#missing\" does not appear in \"storage\""
        ),
        test_cml_offer_bad_from(
            json!({
                    "offer": [ {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "#invalid@",
                        "to": [ "#echo_server" ],
                    } ]
                }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"#invalid@\", expected one or an array of \"realm\", \"framework\", \"self\", or \"#<child-name>\""
        ),
        test_cml_offer_invalid_multiple_from(
            json!({
                    "offer": [ {
                        "protocol": "/svc/fuchsia.logger.Log",
                        "from": [ "self", "#logger" ],
                        "to": [ "#echo_server" ],
                    } ],
                    "children": [
                        {
                            "name": "logger",
                            "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                        },
                        {
                            "name": "echo_server",
                            "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm",
                        },
                    ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"protocol\" capabilities cannot have multiple \"from\" clauses"
        ),
        test_cml_storage_offer_bad_to(
            json!({
                    "offer": [ {
                        "storage": "cache",
                        "from": "realm",
                        "to": [ "#logger" ],
                        "as": "/invalid",
                    } ],
                    "children": [ {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm"
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field cannot be used for storage offer targets"
        ),
        test_cml_offer_empty_targets(
            json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": []
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a nonempty array of offer targets, with unique elements"
        ),
        test_cml_offer_duplicate_targets(
            json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": ["#a", "#a"]
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: array with duplicate element, expected a nonempty array of offer targets, with unique elements"
        ),
        test_cml_offer_target_missing_props(
            json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "as": "/svc/fuchsia.logger.SysLog",
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `to`"
        ),
        test_cml_offer_target_missing_to(
            json!({
                "offer": [ {
                    "service": "/snvc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [ "#missing" ],
                } ],
                "children": [ {
                    "name": "logger",
                    "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"#missing\" is an \"offer\" target but it does not appear in \"children\" or \"collections\""
        ),
        test_cml_offer_target_bad_to(
            json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [ "self" ],
                    "as": "/svc/fuchsia.logger.SysLog",
                } ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"self\", expected \"realm\", \"framework\", \"self\", \"#<child-name>\", or \"#<collection-name>\""
        ),
        test_cml_offer_empty_protocols(
            json!({
                "offer": [
                    {
                        "protocol": [],
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "as": "/thing"
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a path or nonempty array of paths, with unique elements"
        ),
        test_cml_offer_target_equals_from(
            json!({
                "offer": [ {
                    "service": "/svc/fuchsia.logger.Log",
                    "from": "#logger",
                    "to": [ "#logger" ],
                    "as": "/svc/fuchsia.logger.SysLog",
                } ],
                "children": [ {
                    "name": "logger", "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                } ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Offer target \"#logger\" is same as source"
        ),
        test_cml_storage_offer_target_equals_from(
            json!({
                "offer": [ {
                    "storage": "data",
                    "from": "#minfs",
                    "to": [ "#logger" ],
                } ],
                "children": [ {
                    "name": "logger",
                    "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                } ],
                "storage": [ {
                    "name": "minfs",
                    "from": "#logger",
                    "path": "/minfs",
                } ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Storage offer target \"#logger\" is same as source"
        ),
        test_cml_offer_duplicate_target_paths(
            json!({
                "offer": [
                    {
                        "service": "/svc/logger",
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "as": "/thing"
                    },
                    {
                        "service": "/svc/logger",
                        "from": "self",
                        "to": [ "#scenic" ],
                    },
                    {
                        "directory": "/thing",
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    }
                ],
                "children": [
                    {
                        "name": "scenic",
                        "url": "fuchsia-pkg://fuchsia.com/scenic/stable#meta/scenic.cm"
                    },
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"/thing\" is a duplicate \"offer\" target directory for \"#echo_server\""
        ),
        test_cml_offer_duplicate_storage_types(
            json!({
                "offer": [
                    {
                        "storage": "cache",
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "storage": "cache",
                        "from": "#minfs",
                        "to": [ "#echo_server" ]
                    }
                ],
                "storage": [ {
                    "name": "minfs",
                    "from": "self",
                    "path": "/minfs"
                } ],
                "children": [ {
                    "name": "echo_server",
                    "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"cache\" is a duplicate \"offer\" target storage type for \"#echo_server\""
        ),
        test_cml_offer_duplicate_runner_name(
            json!({
                "offer": [
                    {
                        "runner": "elf",
                        "from": "realm",
                        "to": [ "#echo_server" ]
                    },
                    {
                        "runner": "elf",
                        "from": "framework",
                        "to": [ "#echo_server" ]
                    }
                ],
                "children": [ {
                    "name": "echo_server",
                    "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                } ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"elf\" is a duplicate \"offer\" target runner for \"#echo_server\""
        ),
        // if "as" is specified, only 1 "protocol" array item is allowed.
        test_cml_offer_bad_as(
            json!({
                "offer": [
                    {
                        "protocol": ["/svc/A", "/svc/B"],
                        "from": "self",
                        "to": [ "#echo_server" ],
                        "as": "/thing"
                    },
                ],
                "children": [
                    {
                        "name": "echo_server",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"as\" field can only be specified when one `protocol` is supplied."
        ),
        test_cml_offer_bad_subdir(
            json!({
                "offer": [
                    {
                        "directory": "/data/index",
                        "subdir": "/",
                        "from": "realm",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_offer_resolver_from_self(
            json!({
                "offer": [
                    {
                        "resolver": "pkg_resolver",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    },
                ],
                "resolvers": [
                    {
                        "name": "pkg_resolver",
                        "path": "/svc/fuchsia.sys2.ComponentResolver",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_offer_resolver_from_self_missing(
            json!({
                "offer": [
                    {
                        "resolver": "pkg_resolver",
                        "from": "self",
                        "to": [ "#modular" ],
                    },
                ],
                "children": [
                    {
                        "name": "modular",
                        "url": "fuchsia-pkg://fuchsia.com/modular#meta/modular.cm"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Resolver \"pkg_resolver\" is offered from self, so it must be declared in \"resolvers\""
        ),
        test_cml_offer_dependency_on_wrong_type(
            json!({
                    "offer": [ {
                        "service": "/svc/fuchsia.logger.Log",
                        "from": "realm",
                        "to": [ "#echo_server" ],
                        "dependency": "strong"
                    } ],
                    "children": [ {
                            "name": "echo_server",
                            "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Dependency can only be provided for protocol and directory capabilities"
        ),
        test_cml_offer_dependency_cycle(
            json!({
                    "offer": [
                        {
                            "protocol": "/svc/fuchsia.logger.Log",
                            "from": "#a",
                            "to": [ "#b" ],
                            "dependency": "strong"
                        },
                        {
                            "directory": "/data",
                            "from": "#b",
                            "to": [ "#c" ],
                        },
                        {
                            "service": "/dev/ethernet",
                            "from": "#c",
                            "to": [ "#a" ],
                        },
                        {
                            "runner": "elf",
                            "from": "#b",
                            "to": [ "#d" ],
                        },
                        {
                            "resolver": "http",
                            "from": "#d",
                            "to": [ "#b" ],
                        },
                    ],
                    "children": [
                        {
                            "name": "a",
                            "url": "fuchsia-pkg://fuchsia.com/a#meta/a.cm"
                        },
                        {
                            "name": "b",
                            "url": "fuchsia-pkg://fuchsia.com/b#meta/b.cm"
                        },
                        {
                            "name": "c",
                            "url": "fuchsia-pkg://fuchsia.com/b#meta/c.cm"
                        },
                        {
                            "name": "d",
                            "url": "fuchsia-pkg://fuchsia.com/b#meta/d.cm"
                        },
                    ]
                }),
            Err(Error::Validate {
                schema_name: None,
                err
            }) if &err ==
                "Strong dependency cycles were found. Break the cycle by removing a \
                dependency or marking an offer as weak. Cycles: \
                {{child a -> child b -> child c -> child a}, {child b -> child d -> child b}}"
        ),
        test_cml_offer_weak_dependency_cycle(
            json!({
                    "offer": [
                        {
                            "protocol": "/svc/fuchsia.logger.Log",
                            "from": "#child_a",
                            "to": [ "#child_b" ],
                            "dependency": "weak_for_migration"
                        },
                        {
                            "directory": "/data",
                            "from": "#child_b",
                            "to": [ "#child_a" ],
                        },
                    ],
                    "children": [
                        {
                            "name": "child_a",
                            "url": "fuchsia-pkg://fuchsia.com/child_a#meta/child_a.cm"
                        },
                        {
                            "name": "child_b",
                            "url": "fuchsia-pkg://fuchsia.com/child_b#meta/child_b.cm"
                        },
                    ]
                }),
            Ok(())
        ),
        test_cml_offer_disallows_filter_on_non_events(
            json!({
                "offer": [
                    {
                        "directory": "/foo/bar",
                        "rights": [ "r*" ],
                        "from": "self",
                        "to": [ "#logger" ],
                        "filter": {"path": "/diagnostics"}
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"filter\" can only be used with \"event\""
        ),

        // children
        test_cml_children(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                    {
                        "name": "gmail",
                        "url": "https://www.google.com/gmail",
                        "startup": "eager",
                    },
                    {
                        "name": "echo",
                        "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo.cm",
                        "startup": "lazy",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_children_missing_props(
            json!({
                "children": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `name`"
        ),
        test_cml_children_duplicate_names(
           json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    },
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/beta#meta/logger.cm"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"children\" and once in \"children\""
        ),
        test_cml_children_bad_startup(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "startup": "zzz",
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "unknown variant `zzz`, expected `lazy` or `eager`"
        ),
        test_cml_children_bad_environment(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "environment": "realm",
                    }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"realm\", expected \"#<environment-name>\""
        ),
        test_cml_children_unknown_environment(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "environment": "#foo_env",
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"foo_env\" does not appear in \"environments\""
        ),
        test_cml_children_environment(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                        "environment": "#foo_env",
                    }
                ],
                "environments": [
                    {
                        "name": "foo_env",
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_collections_bad_environment(
            json!({
                "collections": [
                    {
                        "name": "tests",
                        "durability": "transient",
                        "environment": "realm",
                    }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"realm\", expected \"#<environment-name>\""
        ),
        test_cml_collections_unknown_environment(
            json!({
                "collections": [
                    {
                        "name": "tests",
                        "durability": "transient",
                        "environment": "#foo_env",
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"foo_env\" does not appear in \"environments\""
        ),
        test_cml_collections_environment(
            json!({
                "collections": [
                    {
                        "name": "tests",
                        "durability": "transient",
                        "environment": "#foo_env",
                    }
                ],
                "environments": [
                    {
                        "name": "foo_env",
                    }
                ]
            }),
            Ok(())
        ),


        test_cml_environment_timeout(
            json!({
                "environments": [
                    {
                        "name": "foo_env",
                        "__stop_timeout_ms": 10000,
                    }
                ]
            }),
            Ok(())
        ),

        test_cml_environment_bad_timeout(
            json!({
                "environments": [
                    {
                        "name": "foo_env",
                        "__stop_timeout_ms": -3,
                    }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: integer `-3`, expected an unsigned 32-bit integer"
        ),

        // collections
        test_cml_collections(
            json!({
                "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent"
                    },
                    {
                        "name": "tests",
                        "durability": "transient"
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_collections_missing_props(
            json!({
                "collections": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `name`"
        ),
        test_cml_collections_duplicate_names(
           json!({
               "collections": [
                    {
                        "name": "modular",
                        "durability": "persistent"
                    },
                    {
                        "name": "modular",
                        "durability": "transient"
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"modular\" is defined twice, once in \"collections\" and once in \"collections\""
        ),
        test_cml_collections_bad_durability(
            json!({
                "collections": [
                    {
                        "name": "modular",
                        "durability": "zzz",
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "unknown variant `zzz`, expected `persistent` or `transient`"
        ),

        // storage
        test_cml_storage(
            json!({
                "storage": [
                    {
                        "name": "a",
                        "from": "#minfs",
                        "path": "/minfs"
                    },
                    {
                        "name": "b",
                        "from": "realm",
                        "path": "/data"
                    },
                    {
                        "name": "c",
                        "from": "self",
                        "path": "/storage"
                    }
                ],
                "children": [
                    {
                        "name": "minfs",
                        "url": "fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_storage_all_valid_chars(
            json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "url": "https://www.google.com/gmail"
                    },
                ],
                "storage": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-storage",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "path": "/example"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_storage_missing_props(
            json!({
                "storage": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `name`"
        ),
        test_cml_storage_missing_from(
            json!({
                    "storage": [ {
                        "name": "minfs",
                        "from": "#missing",
                        "path": "/minfs"
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"storage\" source \"#missing\" does not appear in \"children\""
        ),

        // runner
        test_cml_runner(
            json!({
                "runner": [
                    {
                        "name": "a",
                        "from": "#minfs",
                        "path": "/minfs"
                    },
                    {
                        "name": "b",
                        "from": "realm",
                        "path": "/data"
                    },
                    {
                        "name": "c",
                        "from": "self",
                        "path": "/runner"
                    }
                ],
                "children": [
                    {
                        "name": "minfs",
                        "url": "fuchsia-pkg://fuchsia.com/minfs/stable#meta/minfs.cm"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_runner_all_valid_chars(
            json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "url": "https://www.google.com/gmail"
                    },
                ],
                "runner": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-runner",
                        "from": "#abcdefghijklmnopqrstuvwxyz0123456789_-from",
                        "path": "/example"
                    }
                ]
            }),
            Ok(())
        ),
        test_cml_runner_missing_props(
            json!({
                "runners": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `name`"
        ),
        test_cml_runner_missing_from(
            json!({
                    "runners": [ {
                        "name": "minfs",
                        "from": "#missing",
                        "path": "/minfs"
                    } ]
                }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"runner\" source \"#missing\" does not appear in \"children\""
        ),

        // environments
        test_cml_environments(
            json!({
                "environments": [
                    {
                        "name": "my_env_a",
                    },
                    {
                        "name": "my_env_b",
                        "extends": "realm",
                    },
                    {
                        "name": "my_env_c",
                        "extends": "none",
                        "__stop_timeout_ms": 8000,
                    },
                ],
            }),
            Ok(())
        ),

        test_invalid_cml_environment_no_stop_timeout(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "none",
                    },
                ],
            }),
            Err(Error::Validate { schema_name: None, err }) if &err ==
                "'__stop_timeout_ms' must be provided if the environment does not extend \
                another environment"
        ),

        test_cml_environment_invalid_extends(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "some_made_up_string",
                    },
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "unknown variant `some_made_up_string`, expected `realm` or `none`"
        ),
        test_cml_environment_missing_props(
            json!({
                "environments": [ {} ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `name`"
        ),

        test_cml_environment_with_runners(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "realm",
                            }
                        ]
                    }
                ],
            }),
            Ok(())
        ),
        test_cml_environment_with_runners_alias(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "realm",
                                "as": "my-dart",
                            }
                        ]
                    }
                ],
            }),
            Ok(())
        ),
        test_cml_environment_with_runners_missing(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "self",
                            }
                        ]
                    }
                ],
                "runners": [
                     {
                         "name": "dart",
                         "path": "/svc/fuchsia.component.Runner",
                         "from": "realm"
                     }
                ],
            }),
            Ok(())
        ),
        test_cml_environment_with_runners_bad_name(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "elf",
                                "from": "realm",
                                "as": "#elf",
                            }
                        ]
                    }
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"#elf\", expected a name containing only alpha-numeric characters or [_-.]"
        ),
        test_cml_environment_with_runners_duplicate_name(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "dart",
                                "from": "realm",
                            },
                            {
                                "runner": "other-dart",
                                "from": "realm",
                                "as": "dart",
                            }
                        ]
                    }
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Duplicate runners registered under name \"dart\": \"other-dart\" and \"dart\"."
        ),
        test_cml_environment_with_runner_from_missing_child(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "elf",
                                "from": "#missing_child",
                            }
                        ]
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"elf\" runner source \"#missing_child\" does not appear in \"children\""
        ),
        test_cml_environment_with_runner_cycle(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "runners": [
                            {
                                "runner": "elf",
                                "from": "#child",
                                "as": "my-elf",
                            }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "child",
                        "url": "fuchsia-pkg://child",
                        "environment": "#my_env",
                    }
                ]
            }),
            Err(Error::Validate { err, schema_name: None, .. }) if &err ==
                    "Strong dependency cycles were found. Break the cycle by removing a \
                    dependency or marking an offer as weak. Cycles: \
                    {{child child -> environment my_env -> child child}}"
        ),
        test_cml_environment_with_resolvers(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "realm",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ],
            }),
            Ok(())
        ),
        test_cml_environment_with_resolvers_bad_scheme(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "realm",
                                "scheme": "9scheme",
                            }
                        ]
                    }
                ],
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"9scheme\", expected a valid URL scheme"
        ),
        test_cml_environment_with_resolvers_duplicate_scheme(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "realm",
                                "scheme": "fuchsia-pkg",
                            },
                            {
                                "resolver": "base_resolver",
                                "from": "realm",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "scheme \"fuchsia-pkg\" for resolver \"base_resolver\" is already registered; previously registered to resolver \"pkg_resolver\"."
        ),
        test_cml_environment_with_resolver_from_missing_child(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "#missing_child",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"pkg_resolver\" resolver source \"#missing_child\" does not appear in \"children\""
        ),
        test_cml_environment_with_resolver_cycle(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "#child",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "child",
                        "url": "fuchsia-pkg://child",
                        "environment": "#my_env",
                    }
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err ==
                    "Strong dependency cycles were found. Break the cycle by removing a \
                    dependency or marking an offer as weak. \
                    Cycles: {{child child -> environment my_env -> child child}}"
        ),
        test_cml_environment_with_cycle_multiple_components(
            json!({
                "environments": [
                    {
                        "name": "my_env",
                        "extends": "realm",
                        "resolvers": [
                            {
                                "resolver": "pkg_resolver",
                                "from": "#b",
                                "scheme": "fuchsia-pkg",
                            }
                        ]
                    }
                ],
                "children": [
                    {
                        "name": "a",
                        "url": "fuchsia-pkg://a",
                        "environment": "#my_env",
                    },
                    {
                        "name": "b",
                        "url": "fuchsia-pkg://b",
                    }
                ],
                "offer": [
                    {
                        "protocol": "/svc/fuchsia.logger.Log",
                        "from": "#a",
                        "to": [ "#b" ],
                        "dependency": "strong"
                    },
                ]
            }),
            Err(Error::Validate { schema_name: None, err }) if &err ==
                "Strong dependency cycles were found. Break the cycle by removing a dependency \
                or marking an offer as weak. \
                Cycles: {{child a -> child b -> environment my_env -> child a}}"
        ),

        // facets
        test_cml_facets(
            json!({
                "facets": {
                    "metadata": {
                        "title": "foo",
                        "authors": [ "me", "you" ],
                        "year": 2018
                    }
                }
            }),
            Ok(())
        ),
        test_cml_facets_wrong_type(
            json!({
                "facets": 55
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid type: integer `55`, expected a map"
        ),

        // constraints
        test_cml_rights_all(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["connect", "enumerate", "read_bytes", "write_bytes",
                               "execute", "update_attributes", "get_attributes", "traverse",
                               "modify_directory", "admin"],
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_rights_invalid(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["cAnnect", "enumerate"],
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "unknown variant `cAnnect`, expected one of `connect`, `enumerate`, `execute`, `get_attributes`, `modify_directory`, `read_bytes`, `traverse`, `update_attributes`, `write_bytes`, `admin`, `r*`, `w*`, `x*`, `rw*`, `rx*`"
        ),
        test_cml_rights_duplicate(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["connect", "connect"],
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: array with duplicate element, expected a nonempty array of rights, with unique elements"
        ),
        test_cml_rights_empty(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": [],
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a nonempty array of rights, with unique elements"
        ),
        test_cml_rights_alias_star_expansion(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["r*"],
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_rights_alias_star_expansion_with_longform(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["w*", "read_bytes"],
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_rights_alias_star_expansion_with_longform_collision(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["r*", "read_bytes"],
                  },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"read_bytes\" is duplicated in the rights clause."
        ),
        test_cml_rights_alias_star_expansion_collision(
            json!({
                "use": [
                  {
                    "directory": "/foo/bar",
                    "rights": ["w*", "x*"],
                  },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "\"x*\" is duplicated in the rights clause."
        ),
        test_cml_rights_use_invalid(
            json!({
                "use": [
                  { "directory": "/foo", },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Rights required for this use statement."
        ),
        test_cml_rights_offer_dir_invalid(
            json!({
                "offer": [
                  {
                    "directory": "/foo",
                    "from": "self",
                    "to": [ "#echo_server" ],
                  },
                ],
                "children": [
                  {
                    "name": "echo_server",
                    "url": "fuchsia-pkg://fuchsia.com/echo/stable#meta/echo_server.cm"
                  }
                ],
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Rights required for this offer as it is offering from self."
        ),
        test_cml_rights_expose_dir_invalid(
            json!({
                "expose": [
                  {
                    "directory": "/foo/bar",
                    "from": "self",
                  },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Rights required for this expose statement as it is exposing from self."
        ),
        test_cml_path(
            json!({
                "use": [
                  {
                    "directory": "/foo/?!@#$%/Bar",
                    "rights": ["read_bytes"],
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_path_invalid_empty(
            json!({
                "use": [
                  { "service": "" },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a non-empty path no more than 1024 characters in length"
        ),
        test_cml_path_invalid_root(
            json!({
                "use": [
                  { "service": "/" },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with leading `/` and non-empty segments"
        ),
        test_cml_path_invalid_absolute_is_relative(
            json!({
                "use": [
                  { "service": "foo/bar" },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"foo/bar\", expected a path with leading `/` and non-empty segments"
        ),
        test_cml_path_invalid_trailing(
            json!({
                "use": [
                  { "service": "/foo/bar/" },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/foo/bar/\", expected a path with leading `/` and non-empty segments"
        ),
        test_cml_path_too_long(
            json!({
                "use": [
                  { "service": format!("/{}", "a".repeat(1024)) },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 1025, expected a non-empty path no more than 1024 characters in length"
        ),
        test_cml_relative_path(
            json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "?!@#$%/Bar",
                  },
                ]
            }),
            Ok(())
        ),
        test_cml_relative_path_invalid_empty(
            json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "",
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 0, expected a non-empty path no more than 1024 characters in length"
        ),
        test_cml_relative_path_invalid_root(
            json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "/",
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_relative_path_invalid_absolute(
            json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "/bar",
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"/bar\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_relative_path_invalid_trailing(
            json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": "bar/",
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"bar/\", expected a path with no leading `/` and non-empty segments"
        ),
        test_cml_relative_path_too_long(
            json!({
                "use": [
                  {
                    "directory": "/foo",
                    "rights": ["r*"],
                    "subdir": format!("{}", "a".repeat(1025)),
                  },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 1025, expected a non-empty path no more than 1024 characters in length"
        ),
        test_cml_relative_ref_too_long(
            json!({
                "expose": [
                    {
                        "service": "/loggers/fuchsia.logger.Log",
                        "from": &format!("#{}", "a".repeat(101)),
                    },
                ],
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 102, expected one or an array of \"framework\", \"self\", or \"#<child-name>\""
        ),
        test_cml_child_name(
            json!({
                "children": [
                    {
                        "name": "abcdefghijklmnopqrstuvwxyz0123456789_-.",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_child_name_invalid(
            json!({
                "children": [
                    {
                        "name": "#bad",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"#bad\", expected a name containing only alpha-numeric characters or [_-.]"
        ),
        test_cml_child_name_too_long(
            json!({
                "children": [
                    {
                        "name": "a".repeat(101),
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    }
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 101, expected a non-empty name no more than 100 characters in length"
        ),
        test_cml_url(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "my+awesome-scheme.2://abc123!@#$%.com",
                    },
                ]
            }),
            Ok(())
        ),
        test_cml_url_invalid(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid value: string \"fuchsia-pkg\", expected a valid URL"
        ),
        test_cml_url_too_long(
            json!({
                "children": [
                    {
                        "name": "logger",
                        "url": &format!("fuchsia-pkg://{}", "a".repeat(4083)),
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "invalid length 4097, expected a non-empty URL no more than 4096 characters in length"
        ),
        test_cml_duplicate_identifiers_children_collection(
           json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
               ],
               "collections": [
                   {
                       "name": "logger",
                       "durability": "transient"
                   }
               ]
           }),
           Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"collections\" and once in \"children\""
        ),
        test_cml_duplicate_identifiers_children_storage(
           json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
               ],
               "storage": [
                    {
                        "name": "logger",
                        "path": "/logs",
                        "from": "realm"
                    }
                ]
           }),
           Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"storage\" and once in \"children\""
        ),
        test_cml_duplicate_identifiers_collection_storage(
           json!({
               "collections": [
                    {
                        "name": "logger",
                        "durability": "transient"
                    }
                ],
                "storage": [
                    {
                        "name": "logger",
                        "path": "/logs",
                        "from": "realm"
                    }
                ]
           }),
           Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"storage\" and once in \"collections\""
        ),
        test_cml_duplicate_identifiers_children_runners(
           json!({
               "children": [
                    {
                        "name": "logger",
                        "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                    }
               ],
               "runners": [
                    {
                        "name": "logger",
                        "path": "/logs",
                        "from": "realm"
                    }
                ]
           }),
           Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"runners\" and once in \"children\""
        ),
        test_cml_duplicate_identifiers_environments(
            json!({
                "children": [
                     {
                         "name": "logger",
                         "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm"
                     }
                ],
                "environments": [
                     {
                         "name": "logger",
                     }
                 ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"logger\" is defined twice, once in \"environments\" and once in \"children\""
        ),
        test_cml_program_no_runner(
            json!({"program": { "binary": "bin/app" }}),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "Component has a \'program\' block defined, but doesn\'t \'use\' a runner capability. Components need to \'use\' a runner to actually execute code."
        ),

        // Resolvers
        test_cml_resolvers_duplicates(
            json!({
                "resolvers": [
                    {
                        "name": "pkg_resolver",
                        "path": "/svc/fuchsia.sys2.ComponentResolver",
                    },
                    {
                        "name": "pkg_resolver",
                        "path": "/svc/my-resolver",
                    },
                ]
            }),
            Err(Error::Validate { schema_name: None, err, .. }) if &err == "identifier \"pkg_resolver\" is defined twice, once in \"resolvers\" and once in \"resolvers\""
        ),
        test_cml_resolvers_missing_name(
            json!({
                "resolvers": [
                    {
                        "path": "/svc/fuchsia.sys2.ComponentResolver",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `name`"
        ),
        test_cml_resolvers_missing_path(
            json!({
                "resolvers": [
                    {
                        "name": "pkg_resolver",
                    },
                ]
            }),
            Err(Error::Parse { err, .. }) if &err == "missing field `path`"
        ),
    }

    test_validate_cmx! {
        test_cmx_err_empty_json(
            json!({}),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "This property is required at /program"
        ),
        test_cmx_program(
            json!({"program": { "binary": "bin/app" }}),
            Ok(())
        ),
        test_cmx_program_no_binary(
            json!({ "program": {}}),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "OneOf conditions are not met at /program"
        ),
        test_cmx_bad_program(
            json!({"prigram": { "binary": "bin/app" }}),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Property conditions are not met at , This property is required at /program"
        ),
        test_cmx_sandbox(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": { "dev": [ "class/camera" ] }
            }),
            Ok(())
        ),
        test_cmx_facets(
            json!({
                "program": { "binary": "bin/app" },
                "facets": {
                    "fuchsia.test": {
                         "system-services": [ "fuchsia.logger.LogSink" ]
                    }
                }
            }),
            Ok(())
        ),
        test_cmx_block_system_data(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "data" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_data_stem(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "data-should-pass" ]
                }
            }),
            Ok(())
        ),
        test_cmx_block_system_data_leading_slash(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "/data" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_data_subdir(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "data/should-fail" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_deprecated_data(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "deprecated-data" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_deprecated_data_stem(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "deprecated-data-should-pass" ]
                }
            }),
            Ok(())
        ),
        test_cmx_block_system_deprecated_data_leading_slash(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "/deprecated-data" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
        test_cmx_block_system_deprecated_data_subdir(
            json!({
                "program": { "binary": "bin/app" },
                "sandbox": {
                    "system": [ "deprecated-data/should-fail" ]
                }
            }),
            Err(Error::Validate { schema_name: Some(s), err, .. }) if s == *CMX_SCHEMA.name && &err == "Not condition is not met at /sandbox/system/0"
        ),
    }

    // We can't simply using JsonSchema::new here and create a temp file with the schema content
    // to pass to validate() later because the path in the errors in the expected results below
    // need to include the whole path, since that's what you get in the Error::Validate.
    lazy_static! {
        static ref BLOCK_SHELL_FEATURE_SCHEMA: JsonSchema<'static> = str_to_json_schema(
            "block_shell_feature.json",
            include_str!("../test_block_shell_feature.json")
        );
    }
    lazy_static! {
        static ref BLOCK_DEV_SCHEMA: JsonSchema<'static> =
            str_to_json_schema("block_dev.json", include_str!("../test_block_dev.json"));
    }

    fn str_to_json_schema<'a, 'b>(name: &'a str, content: &'a str) -> JsonSchema<'b> {
        lazy_static! {
            static ref TEMPDIR: TempDir = TempDir::new().unwrap();
        }

        let tmp_path = TEMPDIR.path().join(name);
        File::create(&tmp_path).unwrap().write_all(content.as_bytes()).unwrap();
        JsonSchema::new_from_file(&tmp_path).unwrap()
    }

    macro_rules! test_validate_extra_schemas {
        (
            $(
                $test_name:ident($input:expr, $extra_schemas:expr, $($pattern:tt)+),
            )+
        ) => {
            $(
                #[test]
                fn $test_name() -> Result<(), Error> {
                    let tmp_dir = TempDir::new()?;
                    let tmp_cmx_path = tmp_dir.path().join("test.cmx");
                    let input = format!("{}", $input);
                    File::create(&tmp_cmx_path)?.write_all(input.as_bytes())?;
                    let extra_schemas: &[(&JsonSchema<'_>, Option<String>)] = $extra_schemas;
                    let extra_schema_paths: Vec<_> = extra_schemas
                        .iter()
                        .map(|i| (Path::new(&*i.0.name), i.1.clone()))
                        .collect();
                    let result = validate(&[tmp_cmx_path.as_path()], &extra_schema_paths);
                    assert_matches!(result, $($pattern)+);
                    Ok(())
                }
            )+
        }
    }

    test_validate_extra_schemas! {
        test_validate_extra_schemas_empty_json(
            json!({"program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            Ok(())
        ),
        test_validate_extra_schemas_empty_features(
            json!({"sandbox": {"features": []}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            Ok(())
        ),
        test_validate_extra_schemas_feature_not_present(
            json!({"sandbox": {"features": ["isolated-persistent-storage"]}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            Ok(())
        ),
        test_validate_extra_schemas_feature_present(
            json!({"sandbox": {"features" : ["deprecated-shell"]}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None)],
            Err(Error::Validate { schema_name: Some(s), err, .. }) if *s == BLOCK_SHELL_FEATURE_SCHEMA.name && &err == "Not condition is not met at /sandbox/features/0"
        ),
        test_validate_extra_schemas_block_dev(
            json!({"dev": ["misc"], "program": {"binary": "a"}}),
            &[(&BLOCK_DEV_SCHEMA, None)],
            Err(Error::Validate { schema_name: Some(s), err, .. }) if *s == BLOCK_DEV_SCHEMA.name && &err == "Not condition is not met at /dev"
        ),
        test_validate_multiple_extra_schemas_valid(
            json!({"sandbox": {"features": ["isolated-persistent-storage"]}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None), (&BLOCK_DEV_SCHEMA, None)],
            Ok(())
        ),
        test_validate_multiple_extra_schemas_invalid(
            json!({"dev": ["misc"], "sandbox": {"features": ["isolated-persistent-storage"]}, "program": {"binary": "a"}}),
            &[(&BLOCK_SHELL_FEATURE_SCHEMA, None), (&BLOCK_DEV_SCHEMA, None)],
            Err(Error::Validate { schema_name: Some(s), err, .. }) if *s == BLOCK_DEV_SCHEMA.name && &err == "Not condition is not met at /dev"
        ),
        test_validate_extra_error(
            json!({"dev": ["misc"], "program": {"binary": "a"}}),
            &[(&BLOCK_DEV_SCHEMA, Some("Extra error".to_string()))],
            Err(Error::Validate { schema_name: Some(s), err, .. }) if *s == BLOCK_DEV_SCHEMA.name && &err == "Not condition is not met at /dev\nExtra error"
        ),
    }

    fn empty_offer() -> cml::Offer {
        cml::Offer {
            service: None,
            protocol: None,
            directory: None,
            storage: None,
            runner: None,
            resolver: None,
            event: None,
            from: OneOrMany::One(cml::OfferFromRef::Self_),
            to: cml::OfferTo(vec![]),
            r#as: None,
            rights: None,
            subdir: None,
            dependency: None,
            filter: None,
        }
    }

    #[test]
    fn test_capability_id() -> Result<(), Error> {
        // Simple tests.
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                service: Some("/a".parse().unwrap()),
                ..empty_offer()
            })?,
            vec![CapabilityId::Service(&"/a".parse().unwrap())]
        );
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                protocol: Some(OneOrMany::One("/a".parse().unwrap())),
                ..empty_offer()
            })?,
            vec![CapabilityId::Protocol(&"/a".parse().unwrap())]
        );
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                protocol: Some(
                    OneOrMany::Many(vec!["/a".parse().unwrap(), "/b".parse().unwrap()],)
                ),
                ..empty_offer()
            })?,
            vec![
                CapabilityId::Protocol(&"/a".parse().unwrap()),
                CapabilityId::Protocol(&"/b".parse().unwrap())
            ]
        );
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                directory: Some("/a".parse().unwrap()),
                ..empty_offer()
            })?,
            vec![CapabilityId::Directory(&"/a".parse().unwrap())]
        );
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                storage: Some(cml::StorageType::Cache),
                ..empty_offer()
            })?,
            vec![CapabilityId::StorageType(&cml::StorageType::Cache)],
        );

        // "as" aliasing.
        assert_eq!(
            CapabilityId::from_clause(&cml::Offer {
                service: Some("/a".parse().unwrap()),
                r#as: Some("/b".parse().unwrap()),
                ..empty_offer()
            })?,
            vec![CapabilityId::Service(&"/b".parse().unwrap())]
        );

        // Error case.
        assert_matches!(CapabilityId::from_clause(&empty_offer()), Err(_));

        Ok(())
    }
}
